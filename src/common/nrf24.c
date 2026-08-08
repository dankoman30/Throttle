/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Daniel Koman */
#include "nrf24.h"
#include "throttle_protocol.h" /* PACKET_SIZE, for RX_PW_P0 */

/* RF_SETUP = 0x24: RF_DR_LOW=1 (bit5), RF_DR_HIGH=0 (bit3) -> per the
 * datasheet's [RF_DR_LOW,RF_DR_HIGH] encoding table, '10' = 250kbps.
 * RF_PWR='10' (bits 2:1) -> -6dBm, i.e. the TMRh20/RF24 library's
 * RF24_PA_HIGH (one step below the true max of 0dBm). Both bit positions
 * and the encoding table verified against the nRF24L01+ (Plus) datasheet -
 * the original non-Plus nRF24L01 does not support 250kbps at all and has a
 * different RF_SETUP layout, so do not reuse this constant against that
 * chip. */
#define RF_SETUP_250KBPS_PA_HIGH  0x24u

/* 1Mbps + PA_HIGH: RF_DR_LOW=0, RF_DR_HIGH=0 -> '00' = 1Mbps per the same
 * encoding table. Currently what nrf24_init() uses - a bring-up diagnostic
 * finding, not a final decision. See this file's header comment and
 * docs/decisions/0005-radio-choice-and-ignition-emi.md's 2026-08-06
 * addendum: 250kbps produced zero receptions on the (likely clone)
 * bring-up hardware even with the MAX_RT workaround below applied, while
 * 1Mbps worked immediately. Re-test 250kbps on genuine hardware before
 * assuming 1Mbps should stay the production data rate. */
#define RF_SETUP_1MBPS_PA_HIGH  0x06u

/* Placeholder RF channel and address - NOT the final ADR 0005 channel
 * selection (still open, see docs/OPEN-ITEMS.md "Channel selection").
 * Only needs to match on both ends; revisit once real channel-selection
 * testing happens. */
#define PLACEHOLDER_RF_CHANNEL  76u
static const uint8_t PLACEHOLDER_ADDR[5] = { 0xE7, 0xE7, 0xE7, 0xE7, 0xE7 };

#define NRF24_MAX_MULTIBYTE_LEN 5u /* max real use: 5-byte address registers */

static void csn_low(const nrf24_handle_t *nrf)  { HAL_GPIO_WritePin(nrf->csn_port, nrf->csn_pin, GPIO_PIN_RESET); }
static void csn_high(const nrf24_handle_t *nrf) { HAL_GPIO_WritePin(nrf->csn_port, nrf->csn_pin, GPIO_PIN_SET); }
static void ce_low(const nrf24_handle_t *nrf)   { HAL_GPIO_WritePin(nrf->ce_port, nrf->ce_pin, GPIO_PIN_RESET); }
static void ce_high(const nrf24_handle_t *nrf)  { HAL_GPIO_WritePin(nrf->ce_port, nrf->ce_pin, GPIO_PIN_SET); }

uint8_t nrf24_read_reg(const nrf24_handle_t *nrf, uint8_t reg) {
    uint8_t tx[2] = { (uint8_t)(NRF24_CMD_R_REGISTER | (reg & 0x1Fu)), 0xFFu };
    uint8_t rx[2] = { 0, 0 };
    csn_low(nrf);
    HAL_SPI_TransmitReceive(nrf->hspi, tx, rx, 2, 100);
    csn_high(nrf);
    return rx[1];
}

void nrf24_write_reg(const nrf24_handle_t *nrf, uint8_t reg, uint8_t value) {
    uint8_t tx[2] = { (uint8_t)(NRF24_CMD_W_REGISTER | (reg & 0x1Fu)), value };
    uint8_t rx[2];
    csn_low(nrf);
    HAL_SPI_TransmitReceive(nrf->hspi, tx, rx, 2, 100);
    csn_high(nrf);
}

/* Multi-byte registers (e.g. RX_ADDR_P0, TX_ADDR) are LSByte first per the
 * datasheet's SPI command format. Built as one cmd+data buffer and sent in a
 * single HAL_SPI_TransmitReceive call (matching nrf24_read_reg/write_reg) -
 * an earlier per-byte-call implementation read back all-zero on real
 * hardware instead. */
void nrf24_read_reg_n(const nrf24_handle_t *nrf, uint8_t reg, uint8_t *buf, uint8_t len) {
    uint8_t tx[1 + NRF24_MAX_MULTIBYTE_LEN];
    uint8_t rx[1 + NRF24_MAX_MULTIBYTE_LEN];
    tx[0] = (uint8_t)(NRF24_CMD_R_REGISTER | (reg & 0x1Fu));
    for (uint8_t i = 0; i < len; i++) {
        tx[1 + i] = 0xFFu;
    }
    csn_low(nrf);
    HAL_SPI_TransmitReceive(nrf->hspi, tx, rx, (uint16_t)(1 + len), 100);
    csn_high(nrf);
    for (uint8_t i = 0; i < len; i++) {
        buf[i] = rx[1 + i];
    }
}

void nrf24_write_reg_n(const nrf24_handle_t *nrf, uint8_t reg, const uint8_t *buf, uint8_t len) {
    uint8_t tx[1 + NRF24_MAX_MULTIBYTE_LEN];
    uint8_t rx[1 + NRF24_MAX_MULTIBYTE_LEN];
    tx[0] = (uint8_t)(NRF24_CMD_W_REGISTER | (reg & 0x1Fu));
    for (uint8_t i = 0; i < len; i++) {
        tx[1 + i] = buf[i];
    }
    csn_low(nrf);
    HAL_SPI_TransmitReceive(nrf->hspi, tx, rx, (uint16_t)(1 + len), 100);
    csn_high(nrf);
}

void nrf24_flush_tx(const nrf24_handle_t *nrf) {
    uint8_t cmd = NRF24_CMD_FLUSH_TX;
    uint8_t status;
    csn_low(nrf);
    HAL_SPI_TransmitReceive(nrf->hspi, &cmd, &status, 1, 100);
    csn_high(nrf);
}

void nrf24_flush_rx(const nrf24_handle_t *nrf) {
    uint8_t cmd = NRF24_CMD_FLUSH_RX;
    uint8_t status;
    csn_low(nrf);
    HAL_SPI_TransmitReceive(nrf->hspi, &cmd, &status, 1, 100);
    csn_high(nrf);
}

void nrf24_init(const nrf24_handle_t *nrf) {
    ce_low(nrf); /* standby, not TX/RX yet */
    csn_high(nrf);
    HAL_Delay(100); /* let the module's power-on settle */

    /* The chip's own power isn't tied to the MCU's reset/reflash cycle, so
     * STATUS flags and FIFO contents from a previous run can still be
     * sitting there. Per the datasheet, a latched MAX_RT blocks all further
     * transmission until explicitly cleared - clear it (and TX_DS/RX_DR for
     * good measure) and flush both FIFOs before doing anything else. */
    nrf24_write_reg(nrf, NRF24_REG_STATUS, NRF24_STATUS_RX_DR | NRF24_STATUS_TX_DS | NRF24_STATUS_MAX_RT);
    nrf24_flush_tx(nrf);
    nrf24_flush_rx(nrf);

    /* All register writes below happen while PWR_UP is still 0 (the
     * post-reset power-down default) - W_REGISTER is only valid in
     * power-down or standby per the datasheet, so every register must be
     * configured BEFORE the CONFIG write that raises PWR_UP. */
    nrf24_write_reg(nrf, NRF24_REG_EN_AA, 0x00u);           /* no radio-level ack, ADR 0001 */
    nrf24_write_reg(nrf, NRF24_REG_SETUP_RETR, 0x00u);      /* no retries, matches no-ack */
    nrf24_write_reg(nrf, NRF24_REG_RF_CH, PLACEHOLDER_RF_CHANNEL);
    nrf24_write_reg(nrf, NRF24_REG_RF_SETUP, RF_SETUP_1MBPS_PA_HIGH);
    nrf24_write_reg_n(nrf, NRF24_REG_RX_ADDR_P0, PLACEHOLDER_ADDR, 5);
    nrf24_write_reg_n(nrf, NRF24_REG_TX_ADDR, PLACEHOLDER_ADDR, 5);
    nrf24_write_reg(nrf, NRF24_REG_RX_PW_P0, (uint8_t)PACKET_SIZE);

    /* PRIM_RX left 0 (PTX) - matches nrf24_enter_tx_mode()'s state, so a
     * TX-role board needs no further mode call. An RX-role board switches
     * via nrf24_enter_rx_mode() after this. */
    nrf24_write_reg(nrf, NRF24_REG_CONFIG, NRF24_CONFIG_EN_CRC | NRF24_CONFIG_PWR_UP);
    HAL_Delay(2); /* Tpd2stby: power-down -> standby-I, max 1.5ms per datasheet Table 16 */
}

void nrf24_enter_tx_mode(const nrf24_handle_t *nrf) {
    ce_low(nrf);
    uint8_t cfg = nrf24_read_reg(nrf, NRF24_REG_CONFIG);
    nrf24_write_reg(nrf, NRF24_REG_CONFIG, cfg & (uint8_t)~NRF24_CONFIG_PRIM_RX);
}

void nrf24_enter_rx_mode(const nrf24_handle_t *nrf) {
    ce_low(nrf);
    uint8_t cfg = nrf24_read_reg(nrf, NRF24_REG_CONFIG);
    nrf24_write_reg(nrf, NRF24_REG_CONFIG, cfg | NRF24_CONFIG_PRIM_RX);
    ce_high(nrf);
    HAL_Delay(1); /* Tstby2a: standby -> RX settling, max 130us per datasheet Table 16 */
}

void nrf24_send_payload(const nrf24_handle_t *nrf, const uint8_t *data, uint8_t len) {
    uint8_t tx[1 + NRF24_MAX_MULTIBYTE_LEN];
    uint8_t rx[1 + NRF24_MAX_MULTIBYTE_LEN];
    tx[0] = NRF24_CMD_W_TX_PAYLOAD;
    for (uint8_t i = 0; i < len; i++) {
        tx[1 + i] = data[i];
    }
    csn_low(nrf);
    HAL_SPI_TransmitReceive(nrf->hspi, tx, rx, (uint16_t)(1 + len), 100);
    csn_high(nrf);

    /* Pulse CE >= 10us (Thce) to start transmission; the chip completes the
     * send autonomously even after CE returns low. HAL_Delay's 1ms floor is
     * far more than needed but harmless at this project's 80Hz send rate. */
    ce_high(nrf);
    HAL_Delay(1);
    ce_low(nrf);

    /* Workaround for a MAX_RT/TX_FULL lockup seen on hardware even with
     * EN_AA=0x00 and SETUP_RETR=0x00 confirmed via register read-back
     * (should make MAX_RT unreachable per the real nRF24L01+ datasheet).
     * Matches a known issue with likely clone silicon (SI24R1, flagged as a
     * real risk for this exact PA+LNA module category in
     * DEVELOPMENT/radio/README.md) having inverted ACK-related behavior.
     * Clearing STATUS and flushing the TX FIFO after every send, not just
     * once at init, stops a spurious MAX_RT on one send from permanently
     * blocking every send after it. */
    nrf24_write_reg(nrf, NRF24_REG_STATUS, NRF24_STATUS_RX_DR | NRF24_STATUS_TX_DS | NRF24_STATUS_MAX_RT);
    nrf24_flush_tx(nrf);
}

uint8_t nrf24_rx_available(const nrf24_handle_t *nrf) {
    uint8_t fifo_status = nrf24_read_reg(nrf, NRF24_REG_FIFO_STATUS);
    return (fifo_status & NRF24_FIFO_STATUS_RX_EMPTY) ? 0u : 1u;
}

void nrf24_read_payload(const nrf24_handle_t *nrf, uint8_t *data, uint8_t len) {
    uint8_t tx[1 + NRF24_MAX_MULTIBYTE_LEN];
    uint8_t rx[1 + NRF24_MAX_MULTIBYTE_LEN];
    tx[0] = NRF24_CMD_R_RX_PAYLOAD;
    for (uint8_t i = 0; i < len; i++) {
        tx[1 + i] = 0xFFu;
    }
    csn_low(nrf);
    HAL_SPI_TransmitReceive(nrf->hspi, tx, rx, (uint16_t)(1 + len), 100);
    csn_high(nrf);
    for (uint8_t i = 0; i < len; i++) {
        data[i] = rx[1 + i];
    }
    nrf24_write_reg(nrf, NRF24_REG_STATUS, NRF24_STATUS_RX_DR); /* write-1-to-clear */
}
