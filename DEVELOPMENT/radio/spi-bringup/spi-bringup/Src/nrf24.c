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

/* Matches throttle_packet_t's size in src/common/throttle_protocol.h.
 * Hardcoded here rather than including that header directly - this bring-up
 * project is deliberately decoupled from the real protocol contract until
 * the "wire real radio.* calls into handle_firmware.c/receiver_firmware.c"
 * step in DEVELOPMENT/radio/README.md's bring-up order. */
#define BRINGUP_PACKET_SIZE  5u

static void csn_low(void)  { HAL_GPIO_WritePin(NRF_CSN_GPIO_Port, NRF_CSN_Pin, GPIO_PIN_RESET); }
static void csn_high(void) { HAL_GPIO_WritePin(NRF_CSN_GPIO_Port, NRF_CSN_Pin, GPIO_PIN_SET); }

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
 * datasheet's SPI command format. */
void nrf24_read_reg_n(uint8_t reg, uint8_t *buf, uint8_t len) {
    uint8_t cmd = (uint8_t)(NRF24_CMD_R_REGISTER | (reg & 0x1Fu));
    uint8_t status;
    csn_low();
    HAL_SPI_TransmitReceive(&hspi1, &cmd, &status, 1, 100);
    for (uint8_t i = 0; i < len; i++) {
        uint8_t tx = 0xFFu, rxb = 0;
        HAL_SPI_TransmitReceive(&hspi1, &tx, &rxb, 1, 100);
        buf[i] = rxb;
    }
    csn_high();
}

void nrf24_write_reg_n(uint8_t reg, const uint8_t *buf, uint8_t len) {
    uint8_t cmd = (uint8_t)(NRF24_CMD_W_REGISTER | (reg & 0x1Fu));
    uint8_t status;
    csn_low();
    HAL_SPI_TransmitReceive(&hspi1, &cmd, &status, 1, 100);
    for (uint8_t i = 0; i < len; i++) {
        uint8_t tx = buf[i], rxb = 0;
        HAL_SPI_TransmitReceive(&hspi1, &tx, &rxb, 1, 100);
    }
    csn_high();
}

void nrf24_init(void) {
    HAL_GPIO_WritePin(NRF_CE_GPIO_Port, NRF_CE_Pin, GPIO_PIN_RESET); /* standby, not TX/RX yet */
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
    nrf24_write_reg(NRF24_REG_RX_PW_P0, BRINGUP_PACKET_SIZE);

    /* PRIM_RX left 0 (PTX) for now - this bring-up step only proves
     * register read/write through the driver, not actual TX/RX (CE stays
     * low throughout). Role assignment happens in the two-board test. */
    nrf24_write_reg(NRF24_REG_CONFIG, NRF24_CONFIG_EN_CRC | NRF24_CONFIG_PWR_UP);
    HAL_Delay(2); /* Tpd2stby: power-down -> standby-I, max 1.5ms per datasheet Table 16 */
}
