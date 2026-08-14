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
 *     read_start_button_raw(), read_cruise_button_raw(),
 *     read_aux1_switch(), and read_aux2_switch() read the real GPIO/ADC
 *     when HANDLE_PROD_BOARD is defined (only true here). These are
 *     genuine inputs the kill/cruise/start logic directly calls and
 *     depends on.
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

#include "led_blink.h"
#include "unit_config.h"
#include "nrf24.c"

#define HANDLE_PROD_BOARD
#include "handle_firmware.c"

/* --- Radio --- */
static nrf24_handle_t g_radio;

/* This board's per-unit nRF24 address (src/common/unit_config.h) - a
 * static array so its lifetime outlives handle_app_init(), since
 * nrf24_handle_t.addr only points to it rather than copying it. */
static const uint8_t g_unit_addr[5] = { UNIT_NRF24_ADDR_PREFIX, UNIT_NUMBER };

/* --- Status LED: see handle_app.h for the full color/priority writeup.
 * Same priority-table shape and src/common/led_blink.h patterns as the
 * receiver's prod_status_led_tick(), so both boards' red channel means
 * the same thing with byte-identical timing. --- */
static blink_state_t g_handle_kill_blink = {0, 0};
static blink_state_t g_handle_heartbeat_blink = {0, 0};

/* Running-proxy latch: no telemetry exists to confirm the engine is
 * actually running, so this is a local-only proxy - a fresh rising edge of
 * g_start_hold_confirmed clears it (starting a new attempt), a falling
 * edge (release) sets it (presumed running), kill also clears it. Mirrors
 * the receiver's own running-proxy rule (voluntary release, tracked
 * there) as closely as this board's local-only information allows. */
static bool g_handle_running_proxy = false;
static bool g_handle_prev_start_hold = false;

static void handle_app_update_running_proxy(void) {
    bool start_hold_now = g_start_hold_confirmed;
    if (g_kill_latched) {
        g_handle_running_proxy = false;
    } else if (start_hold_now && !g_handle_prev_start_hold) {
        g_handle_running_proxy = false; /* fresh crank attempt */
    } else if (!start_hold_now && g_handle_prev_start_hold) {
        g_handle_running_proxy = true; /* released -> presumed running */
    }
    g_handle_prev_start_hold = start_hold_now;
}

static void handle_app_status_led_tick(uint32_t now) {
    handle_app_update_running_proxy();

    bool red = false, green = false;
    if (g_kill_latched) {
        red = blink_pattern_tick(KILL_PATTERN, KILL_PATTERN_LEN, &g_handle_kill_blink, now, 100u);
    } else if (g_start_hold_confirmed) {
        red = true;
        green = true; /* yellow */
    } else if (g_handle_running_proxy) {
        green = true;
    } else {
        bool beat = blink_pattern_tick(HEARTBEAT_PATTERN, HEARTBEAT_PATTERN_LEN, &g_handle_heartbeat_blink, now, 100u);
        /* Test builds (UNIT_NUMBER == 0xFF) invert the heartbeat: mostly ON
         * with brief OFF blips, instead of mostly off with brief pulses -
         * an unmistakable "this is not a real paired unit" visual on real
         * hardware. See src/common/unit_config.h. */
        red = UNIT_IS_TEST_BUILD ? !beat : beat;
    }

    HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, red ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, green ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* Cruise indicator: separate, dedicated LED (physically relocated off the
 * old tri-color package's blue lead - see docs/wiring.md), reading the
 * existing g_cruise_engaged - no logic change from before, just relocated
 * off the status LED. */
static void handle_app_cruise_led_tick(void) {
    HAL_GPIO_WritePin(CRUISE_LED_GPIO_Port, CRUISE_LED_Pin, g_cruise_engaged ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* AUX1/AUX2 indicator LEDs: mirror what THIS board is commanding, not
 * confirmation the receiver acted on it (no telemetry downlink exists).
 * AUX1 mirrors the latched toggle state, AUX2 the live momentary level. */
static void handle_app_aux_led_tick(void) {
    HAL_GPIO_WritePin(AUX1_LED_GPIO_Port, AUX1_LED_Pin, g_aux1_engaged ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(AUX2_LED_GPIO_Port, AUX2_LED_Pin, g_aux2_state ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void handle_app_init(void) {
    g_radio.hspi     = &hspi1;
    g_radio.ce_port  = NRF_CE_GPIO_Port;
    g_radio.ce_pin   = NRF_CE_Pin;
    g_radio.csn_port = NRF_CSN_GPIO_Port;
    g_radio.csn_pin  = NRF_CSN_Pin;
    g_radio.addr     = g_unit_addr;
    nrf24_init(&g_radio); /* leaves the chip ready to transmit (PTX, CE low) */

    HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(CRUISE_LED_GPIO_Port, CRUISE_LED_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(AUX1_LED_GPIO_Port, AUX1_LED_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(AUX2_LED_GPIO_Port, AUX2_LED_Pin, GPIO_PIN_RESET);
}

void handle_app_tick(void) {
    uint32_t now = HAL_GetTick();

    static uint32_t last_tx_ms = 0;
    if ((now - last_tx_ms) >= HANDLE_TX_PERIOD_MS) {
        last_tx_ms = now;
        build_packet();
        nrf24_send_payload(&g_radio, (const uint8_t *)&g_last_packet, PACKET_SIZE);
    }

    handle_app_status_led_tick(now);
    handle_app_cruise_led_tick();
    handle_app_aux_led_tick();
}
