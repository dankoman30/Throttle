/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Daniel Koman */
/* ---------------------------------------------------------------------
 * handle_app.c - production handle/transmitter (see DEVELOPMENT/handle/
 * README.md and DEVELOPMENT/radio/README.md)
 *
 * Feeds the REAL handle kill-latch/cruise/start-hold logic from actual
 * GPIO/ADC reads and drives the real nRF24L01+ TX, mirroring
 * DEVELOPMENT/receiver/receiver-prod/Src/prod_app.c's #include-reuse
 * pattern for the receiver.
 *
 * src/handle/handle_firmware.c is pulled in below via #include and is NOT
 * modified for this board beyond small, reviewed exceptions made upstream
 * in that file itself (not here):
 *   - millis() calls HAL_GetTick() when USE_HAL_DRIVER is defined (same
 *     hardware-wrapper convention as receiver_firmware.c).
 *   - read_throttle_position()'s raw ADC read, read_kill_switch(),
 *     read_start_button_raw(), read_cruise_button_raw(), and
 *     read_aux1_switch() read the real GPIO/ADC when HANDLE_PROD_BOARD is
 *     defined (only true here). These are genuine inputs the kill/cruise/
 *     start logic directly calls and depends on.
 *   - build_packet() (renamed from build_and_send_packet(): it no longer
 *     sends anything) stores its result into g_last_packet instead of
 *     calling the radio driver inline, so this file can send it
 *     externally - the same externally-observed-state pattern
 *     receiver_firmware.c uses for its outputs (g_current_servo_throttle,
 *     g_aux1_state, ...), just applied to an output that happens to be
 *     built inside the shared file instead of a bare stub.
 *
 * Everything else in handle_firmware.c - debounce_update(), kill_confirmed(),
 * apply_cruise(), start_request_confirmed(), the state globals - is used
 * exactly as shipped, because #include-ing the .c file puts all of it,
 * `static` or not, into this same translation unit.
 * ------------------------------------------------------------------- */

#include "handle_app.h"
#include "main.h"
#include "adc.h"
#include "spi.h"

#include "nrf24.c"

#define HANDLE_PROD_BOARD
#include "handle_firmware.c"

/* --- Radio --- */
static nrf24_handle_t g_radio;

/* --- Status LED: see handle_app.h for the full color/priority writeup --- */
#define LED_BLINK_PERIOD_MS 200u

static void handle_app_led_tick(uint32_t now) {
    static uint32_t last_blink_ms = 0;
    static bool blink_on = false;

    bool red = false, green = false, blue = false;

    if (g_kill_latched) {
        if ((now - last_blink_ms) >= LED_BLINK_PERIOD_MS) {
            last_blink_ms = now;
            blink_on = !blink_on;
        }
        red = blink_on;
    } else if (g_cruise_engaged) {
        blue = true;
    } else if (g_start_hold_confirmed) {
        red = true;
        green = true; /* yellow */
    } else {
        green = true;
    }

    HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, red ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, green ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin, blue ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void handle_app_init(void) {
    g_radio.hspi     = &hspi1;
    g_radio.ce_port  = NRF_CE_GPIO_Port;
    g_radio.ce_pin   = NRF_CE_Pin;
    g_radio.csn_port = NRF_CSN_GPIO_Port;
    g_radio.csn_pin  = NRF_CSN_Pin;
    nrf24_init(&g_radio); /* leaves the chip ready to transmit (PTX, CE low) */

    HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin, GPIO_PIN_RESET);
}

void handle_app_tick(void) {
    uint32_t now = HAL_GetTick();

    static uint32_t last_tx_ms = 0;
    if ((now - last_tx_ms) >= HANDLE_TX_PERIOD_MS) {
        last_tx_ms = now;
        build_packet();
        nrf24_send_payload(&g_radio, (const uint8_t *)&g_last_packet, PACKET_SIZE);
    }

    handle_app_led_tick(now);
}
