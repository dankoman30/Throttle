/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Daniel Koman */
#include "nrf24.h"
#include "main.h"
#include "spi.h"

/* RF_SETUP = 0x24: RF_DR_LOW=1 (bit5), RF_DR_HIGH=0 (bit3) -> per the
 * datasheet's [RF_DR_LOW,RF_DR_HIGH] encoding table, '10' = 250kbps.
 * RF_PWR='10' (bits 2:1) -> -6dBm, i.e. the TMRh20/RF24 library's
 * RF24_PA_HIGH (one step below the true max of 0dBm). Both bit positions
 * and the encoding table verified against the nRF24L01+ (Plus) datasheet -
 * the original non-Plus nRF24L01 does not support 250kbps at all and has a
 * different RF_SETUP layout, so do not reuse this constant against that
 * chip. */
#define RF_SETUP_250KBPS_PA_HIGH  0x24u

/* Placeholder RF channel and address - NOT the final ADR 0005 channel
 * selection (still open, see docs/OPEN-ITEMS.md "Channel selection").
 * Only needs to match on both ends of the eventual two-board TX/RX smoke
 * test; revisit once real channel-selection testing happens. */
#define BRINGUP_RF_CHANNEL  76u
static const uint8_t BRINGUP_ADDR[5] = { 0xE7, 0xE7, 0xE7, 0xE7, 0xE7 };

static void csn_low(void)  { HAL_GPIO_WritePin(NRF_CSN_GPIO_Port, NRF_CSN_Pin, GPIO_PIN_RESET); }
static void csn_high(void) { HAL_GPIO_WritePin(NRF_CSN_GPIO_Port, NRF_CSN_Pin, GPIO_PIN_SET); }
static void ce_low(void)   { HAL_GPIO_WritePin(NRF_CE_GPIO_Port, NRF_CE_Pin, GPIO_PIN_RESET); }
static void ce_high(void)  { HAL_GPIO_WritePin(NRF_CE_GPIO_Port, NRF_CE_Pin, GPIO_PIN_SET); }

uint8_t nrf24_read_reg(uint8_t reg) {
    uint8_t tx[2] = { (uint8_t)(NRF24_CMD_R_REGISTER | (reg & 0x1Fu)), 0xFFu };
    uint8_t rx[2] = { 0, 0 };
    csn_low();
    HAL_SPI_TransmitReceive(&hspi1, tx, rx, 2, 100);
    csn_high();
    return rx[1];
}

void nrf24_write_reg(uint8_t reg, uint8_t value) {
    uint8_t tx[2] = { (uint8_t)(NRF24_CMD_W_REGISTER | (reg & 0x1Fu)), value };
    uint8_t rx[2];
    csn_low();
    HAL_SPI_TransmitReceive(&hspi1, tx, rx, 2, 100);
    csn_high();
}

/* Multi-byte registers (e.g. RX_ADDR_P0, TX_ADDR) are LSByte first per the
 * datasheet's SPI command format. Built as one cmd+data buffer and sent in a
 * single HAL_SPI_TransmitReceive call (matching nrf24_read_reg/write_reg),
 * confirmed against real hardware - see DEVELOPMENT/radio/README.md. */
#define NRF24_MAX_MULTIBYTE_LEN 5u /* max real use: 5-byte address registers */

void nrf24_read_reg_n(uint8_t reg, uint8_t *buf, uint8_t len) {
    uint8_t tx[1 + NRF24_MAX_MULTIBYTE_LEN];
    uint8_t rx[1 + NRF24_MAX_MULTIBYTE_LEN];
    tx[0] = (uint8_t)(NRF24_CMD_R_REGISTER | (reg & 0x1Fu));
    for (uint8_t i = 0; i < len; i++) {
        tx[1 + i] = 0xFFu;
    }
    csn_low();
    HAL_SPI_TransmitReceive(&hspi1, tx, rx, (uint16_t)(1 + len), 100);
    csn_high();
    for (uint8_t i = 0; i < len; i++) {
        buf[i] = rx[1 + i];
    }
}

void nrf24_write_reg_n(uint8_t reg, const uint8_t *buf, uint8_t len) {
    uint8_t tx[1 + NRF24_MAX_MULTIBYTE_LEN];
    uint8_t rx[1 + NRF24_MAX_MULTIBYTE_LEN];
    tx[0] = (uint8_t)(NRF24_CMD_W_REGISTER | (reg & 0x1Fu));
    for (uint8_t i = 0; i < len; i++) {
        tx[1 + i] = buf[i];
    }
    csn_low();
    HAL_SPI_TransmitReceive(&hspi1, tx, rx, (uint16_t)(1 + len), 100);
    csn_high();
}

void nrf24_init(void) {
    ce_low(); /* standby, not TX/RX yet */
    csn_high();
    HAL_Delay(100); /* let the module's power-on settle */

    /* All register writes below happen while PWR_UP is still 0 (the
     * post-reset power-down default) - W_REGISTER is only valid in
     * power-down or standby per the datasheet, so every register must be
     * configured BEFORE the CONFIG write that raises PWR_UP. */
    nrf24_write_reg(NRF24_REG_EN_AA, 0x00u);           /* no radio-level ack, ADR 0001 */
    nrf24_write_reg(NRF24_REG_SETUP_RETR, 0x00u);      /* no retries, matches no-ack */
    nrf24_write_reg(NRF24_REG_RF_CH, BRINGUP_RF_CHANNEL);
    nrf24_write_reg(NRF24_REG_RF_SETUP, RF_SETUP_250KBPS_PA_HIGH);
    nrf24_write_reg_n(NRF24_REG_RX_ADDR_P0, BRINGUP_ADDR, 5);
    nrf24_write_reg_n(NRF24_REG_TX_ADDR, BRINGUP_ADDR, 5);
    nrf24_write_reg(NRF24_REG_RX_PW_P0, NRF24_PAYLOAD_SIZE);

    /* PRIM_RX left 0 (PTX) - matches nrf24_enter_tx_mode()'s state, so a
     * TX-role board needs no further mode call. An RX-role board switches
     * via nrf24_enter_rx_mode() after this. */
    nrf24_write_reg(NRF24_REG_CONFIG, NRF24_CONFIG_EN_CRC | NRF24_CONFIG_PWR_UP);
    HAL_Delay(2); /* Tpd2stby: power-down -> standby-I, max 1.5ms per datasheet Table 16 */
}

void nrf24_enter_tx_mode(void) {
    ce_low();
    uint8_t cfg = nrf24_read_reg(NRF24_REG_CONFIG);
    nrf24_write_reg(NRF24_REG_CONFIG, cfg & (uint8_t)~NRF24_CONFIG_PRIM_RX);
}

void nrf24_enter_rx_mode(void) {
    ce_low();
    uint8_t cfg = nrf24_read_reg(NRF24_REG_CONFIG);
    nrf24_write_reg(NRF24_REG_CONFIG, cfg | NRF24_CONFIG_PRIM_RX);
    ce_high();
    HAL_Delay(1); /* Tstby2a: standby -> RX settling, max 130us per datasheet Table 16 */
}

void nrf24_send_payload(const uint8_t *data, uint8_t len) {
    uint8_t tx[1 + NRF24_MAX_MULTIBYTE_LEN];
    uint8_t rx[1 + NRF24_MAX_MULTIBYTE_LEN];
    tx[0] = NRF24_CMD_W_TX_PAYLOAD;
    for (uint8_t i = 0; i < len; i++) {
        tx[1 + i] = data[i];
    }
    csn_low();
    HAL_SPI_TransmitReceive(&hspi1, tx, rx, (uint16_t)(1 + len), 100);
    csn_high();

    /* Pulse CE >= 10us (Thce) to start transmission; the chip completes the
     * send autonomously even after CE returns low. HAL_Delay's 1ms floor is
     * far more than needed but harmless at this test's slow send rate. */
    ce_high();
    HAL_Delay(1);
    ce_low();
}

uint8_t nrf24_rx_available(void) {
    uint8_t fifo_status = nrf24_read_reg(NRF24_REG_FIFO_STATUS);
    return (fifo_status & NRF24_FIFO_STATUS_RX_EMPTY) ? 0u : 1u;
}

void nrf24_read_payload(uint8_t *data, uint8_t len) {
    uint8_t tx[1 + NRF24_MAX_MULTIBYTE_LEN];
    uint8_t rx[1 + NRF24_MAX_MULTIBYTE_LEN];
    tx[0] = NRF24_CMD_R_RX_PAYLOAD;
    for (uint8_t i = 0; i < len; i++) {
        tx[1 + i] = 0xFFu;
    }
    csn_low();
    HAL_SPI_TransmitReceive(&hspi1, tx, rx, (uint16_t)(1 + len), 100);
    csn_high();
    for (uint8_t i = 0; i < len; i++) {
        data[i] = rx[1 + i];
    }
    nrf24_write_reg(NRF24_REG_STATUS, NRF24_STATUS_RX_DR); /* write-1-to-clear */
}
