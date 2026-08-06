/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Daniel Koman */
/* ---------------------------------------------------------------------
 * Minimal nRF24L01+ register-level driver.
 *
 * Implements only what this project's link actually needs: fixed 5-byte
 * payloads, one pipe, no auto-ack, no dynamic payload, poll-based
 * available()/read() (no IRQ pin wired). All command bytes and register
 * addresses below are verified against the nRF24L01+ Product Specification
 * v1.0 (DEVELOPMENT/assets/nRF24L01+/nRF24L01P_Product_Specification_1_0.pdf),
 * not the older non-Plus nRF24L01 datasheet (the two differ in RF_SETUP's
 * bit layout for the 250kbps data rate).
 * ------------------------------------------------------------------- */
#ifndef NRF24_H
#define NRF24_H

#include <stdint.h>

/* --- SPI command bytes (datasheet Table 20) --- */
#define NRF24_CMD_R_REGISTER   0x00u /* OR with a 5-bit register address */
#define NRF24_CMD_W_REGISTER   0x20u /* OR with a 5-bit register address; power-down/standby only */
#define NRF24_CMD_R_RX_PAYLOAD 0x61u
#define NRF24_CMD_W_TX_PAYLOAD 0xA0u
#define NRF24_CMD_FLUSH_TX     0xE1u
#define NRF24_CMD_FLUSH_RX     0xE2u
#define NRF24_CMD_NOP          0xFFu

/* --- Register addresses actually used (datasheet Table 28) --- */
#define NRF24_REG_CONFIG       0x00u
#define NRF24_REG_EN_AA        0x01u
#define NRF24_REG_SETUP_RETR   0x04u
#define NRF24_REG_RF_CH        0x05u
#define NRF24_REG_RF_SETUP     0x06u
#define NRF24_REG_STATUS       0x07u
#define NRF24_REG_RX_ADDR_P0   0x0Au
#define NRF24_REG_TX_ADDR      0x10u
#define NRF24_REG_RX_PW_P0     0x11u
#define NRF24_REG_FIFO_STATUS  0x17u

/* --- CONFIG register bits --- */
#define NRF24_CONFIG_EN_CRC    (1u << 3)
#define NRF24_CONFIG_PWR_UP    (1u << 1)
#define NRF24_CONFIG_PRIM_RX   (1u << 0)

/* --- FIFO_STATUS register bits --- */
#define NRF24_FIFO_STATUS_RX_EMPTY (1u << 0)
#define NRF24_FIFO_STATUS_TX_EMPTY (1u << 4)

/* --- STATUS register bits --- */
#define NRF24_STATUS_RX_DR     (1u << 6)
#define NRF24_STATUS_TX_DS     (1u << 5)
#define NRF24_STATUS_MAX_RT    (1u << 4)

/* Fixed payload size this project's link uses (matches RX_PW_P0, written by
 * nrf24_init()). Public so callers can size their TX/RX buffers. Matches
 * throttle_packet_t's size in src/common/throttle_protocol.h; hardcoded
 * here rather than including that header directly - this bring-up project
 * is deliberately decoupled from the real protocol contract until the
 * "wire real radio.* calls into handle_firmware.c/receiver_firmware.c" step
 * in DEVELOPMENT/radio/README.md's bring-up order. */
#define NRF24_PAYLOAD_SIZE     5u

/* Configures the chip for this project's link: EN_AA=0x00 (no radio-level
 * ack, ADR 0001), no retries, 250kbps + PA_HIGH (-6dBm), a fixed 5-byte
 * static payload on pipe 0, matching TX_ADDR/RX_ADDR_P0. Leaves the chip in
 * standby-I with CE low, PRIM_RX=0 (PTX) - the same state
 * nrf24_enter_tx_mode() puts it in, so a TX-role board needs no further
 * mode call before nrf24_send_payload(). An RX-role board must call
 * nrf24_enter_rx_mode() before nrf24_rx_available()/nrf24_read_payload(). */
void nrf24_init(void);

uint8_t nrf24_read_reg(uint8_t reg);
void nrf24_write_reg(uint8_t reg, uint8_t value);
void nrf24_read_reg_n(uint8_t reg, uint8_t *buf, uint8_t len);
void nrf24_write_reg_n(uint8_t reg, const uint8_t *buf, uint8_t len);

/* FLUSH_TX / FLUSH_RX: discard the entire TX or RX FIFO. */
void nrf24_flush_tx(void);
void nrf24_flush_rx(void);

/* Standby-I as PTX: CE low, CONFIG PRIM_RX=0. Only needed if the chip was
 * previously switched to RX mode - nrf24_init() already leaves it here. */
void nrf24_enter_tx_mode(void);

/* RX Mode: CONFIG PRIM_RX=1 (written while CE is still low, i.e. still in
 * standby, per the "register writes only in power-down/standby" rule), then
 * CE raised high so the chip continuously listens. */
void nrf24_enter_rx_mode(void);

/* Writes len bytes to the TX FIFO (W_TX_PAYLOAD) and pulses CE to start
 * transmission. Call only in TX mode. Blocks for the CE pulse width, not
 * for the transmission itself - the chip finishes the send autonomously
 * after CE returns low. */
void nrf24_send_payload(const uint8_t *data, uint8_t len);

/* True if the RX FIFO has a packet waiting (FIFO_STATUS RX_EMPTY == 0).
 * Call only in RX mode. */
uint8_t nrf24_rx_available(void);

/* Reads len bytes from the RX FIFO (R_RX_PAYLOAD) and clears STATUS.RX_DR.
 * Call only after nrf24_rx_available() returns true. */
void nrf24_read_payload(uint8_t *data, uint8_t len);

#endif /* NRF24_H */
