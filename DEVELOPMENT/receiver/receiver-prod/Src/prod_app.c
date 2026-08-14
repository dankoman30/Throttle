/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Daniel Koman */
/* ---------------------------------------------------------------------
 * prod_app.c - production receiver (see DEVELOPMENT/receiver/README.md
 * and DEVELOPMENT/radio/README.md)
 *
 * Feeds the REAL receiver safety state machine from the actual nRF24L01+
 * radio link instead of breadboard buttons/a pot (compare
 * DEVELOPMENT/receiver/firmware/Src/bench_app.c, the bench rig's version of
 * this same #include pattern), and drives real HAL calls for the servo/
 * LED/relay/accessory outputs that receiver_firmware.c only stubs out.
 *
 * src/receiver/receiver_firmware.c is pulled in below via #include and is
 * NOT modified for this board beyond three small, reviewed exceptions made
 * upstream in that file itself (not here):
 *   - millis() calls HAL_GetTick() when USE_HAL_DRIVER is defined (already
 *     true for the bench rig too - a hardware wrapper, not a safety-logic
 *     change).
 *   - read_local_start_button() and read_battery_mv() read the real
 *     GPIO/ADC when RECEIVER_PROD_BOARD is defined (only true here, not on
 *     the bench rig). Both are genuine inputs the safety/battery logic
 *     directly calls and depends on, unlike the purely-output stubs below
 *     which this file drives externally instead by observing already-
 *     decided state (g_state, g_current_servo_throttle, g_batt_low, ...).
 *   - apply_aux_outputs() stores into g_aux1_state/g_aux2_state/
 *     g_cruise_active so this file can read the latest commanded/observed
 *     state independent of packet arrival, same externally-observed
 *     pattern as everything else. (AUX2 was removed 2026-08-08 to save a
 *     pin on the handle, then restored 2026-08-09 once pins freed up
 *     elsewhere - AUX2_OUT/PA10 was never actually un-assigned in CubeMX,
 *     so no pin changes were needed here, just the code path. g_cruise_active
 *     is purely informational for the cruise indicator LED - see that
 *     global's comment in receiver_firmware.c.)
 *
 * Everything else in receiver_firmware.c - on_packet_received(),
 * handle_valid_packet(), watchdog_tick(), start_tick(), crank_tick(),
 * step_toward_target(), the state machine globals - is used exactly as
 * shipped, because #include-ing the .c file puts all of it, `static` or
 * not, into this same translation unit.
 * ------------------------------------------------------------------- */

#include "prod_app.h"
#include "main.h"
#include "adc.h"
#include "spi.h"
#include "tim.h"

#include "led_blink.h"
#include "unit_config.h"
#include "nrf24.c"

#define RECEIVER_PROD_BOARD
#include "receiver_firmware.c"

/* --- Radio --- */
static nrf24_handle_t g_radio;

/* This board's per-unit nRF24 address (src/common/unit_config.h) - a
 * static array so its lifetime outlives prod_app_init(), since
 * nrf24_handle_t.addr only points to it rather than copying it. */
static const uint8_t g_unit_addr[5] = { UNIT_NRF24_ADDR_PREFIX, UNIT_NUMBER };

/* --- Ingestion: real nRF24L01+ payload -> the actual validation path.
 * Polled every call, no rate limit needed - a FIFO_STATUS check is a cheap
 * 2-byte SPI transaction, same as the bring-up RX role's tight polling
 * loop (DEVELOPMENT/radio/spi-bringup/), which worked fine unthrottled. */
static void prod_ingestion_tick(void) {
    if (!nrf24_rx_available(&g_radio)) {
        return;
    }
    uint8_t raw[PACKET_SIZE];
    nrf24_read_payload(&g_radio, raw, PACKET_SIZE);
    on_packet_received(raw, PACKET_SIZE); /* real sync/CRC/sequence validation */
}

/* --- Actuation: servo PWM, status LED, kill relay, starter relay,
 * accessory outputs. See prod_app.h for the full LED behavior writeup. --- */
#define SERVO_PULSE_MIN_US   1000u
#define SERVO_PULSE_MAX_US   2000u

static void prod_actuation_tick(uint32_t now) {
    /* Servo: read the already-rate-limited/ramped global, same value the
     * real firmware would hand to its own set_servo_throttle() stub - see
     * docs/OPEN-ITEMS.md "Servo PWM mapping" for the SG90-placeholder note
     * on these two constants. */
    uint32_t pulse_us = SERVO_PULSE_MIN_US +
        ((uint32_t)g_current_servo_throttle * (SERVO_PULSE_MAX_US - SERVO_PULSE_MIN_US)) / 255u;
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, pulse_us);

    /* Kill relay: energize-to-kill (see docs/OPEN-ITEMS.md "Kill driver") -
     * refreshed every tick from g_state, not just on the KILL edge, so a
     * missed/glitched write self-corrects on the next tick. This is the
     * ELECTRONIC kill path, separate from and in addition to the hardwired
     * mechanical master kill switch (see receiver_firmware.c's file header
     * note - that one is intentionally absent from all firmware). */
    HAL_GPIO_WritePin(KILL_RELAY_GPIO_Port, KILL_RELAY_Pin,
                       (g_state == STATE_KILLED) ? GPIO_PIN_SET : GPIO_PIN_RESET);

    /* Starter relay: same continuous-refresh approach, energized only while
     * actively cranking - mirrors the belt-and-suspenders safety net already
     * in the control tick below (set_starter(false) whenever
     * g_state != STATE_STARTING), just driving real hardware instead of
     * that stub. */
    HAL_GPIO_WritePin(STARTER_RELAY_GPIO_Port, STARTER_RELAY_Pin,
                       (g_state == STATE_STARTING) ? GPIO_PIN_SET : GPIO_PIN_RESET);

    /* Accessory outputs: mirror the latest commanded state, stored by
     * apply_aux_outputs() into g_aux1_state/g_aux2_state. */
    HAL_GPIO_WritePin(AUX1_OUT_GPIO_Port, AUX1_OUT_Pin, g_aux1_state ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(AUX2_OUT_GPIO_Port, AUX2_OUT_Pin, g_aux2_state ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* Running-proxy latch: same rule as before - lit only on a VOLUNTARY
 * release from STATE_STARTING, never a forced stop; the only way to tell
 * the two apart from here is g_starter_cooldown_until_ms (a static pulled
 * in via the #include above) - end_crank() only sets it on a forced stop.
 * Read by prod_status_led_tick() below; not written straight to a HAL call
 * itself so it can also gate the heartbeat (see prod_app.h). */
static throttle_state_t g_prod_last_state = STATE_IDLE_SAFE;
static uint32_t g_prod_last_seen_cooldown_until = 0;
static bool g_prod_running_proxy = false;

static void prod_update_running_proxy(uint32_t now) {
    if (g_state != g_prod_last_state) {
        if (g_state == STATE_KILLED || g_state == STATE_STARTING) {
            g_prod_running_proxy = false;
        } else if (g_prod_last_state == STATE_STARTING && g_state == STATE_IDLE_SAFE) {
            bool forced_stop = (g_starter_cooldown_until_ms != g_prod_last_seen_cooldown_until) &&
                                ((int32_t)(g_starter_cooldown_until_ms - now) > 0);
            if (!forced_stop) {
                g_prod_running_proxy = true;
            }
        }
        g_prod_last_state = g_state;
    }
    g_prod_last_seen_cooldown_until = g_starter_cooldown_until_ms;
}

/* Status LED (red+green only - see prod_app.h for the full priority
 * writeup). Kill's long-long and the heartbeat's lub-dub both come from
 * src/common/led_blink.h so this board's timing is byte-identical to the
 * handle's. */
static blink_state_t g_prod_kill_blink = {0, 0};
static blink_state_t g_prod_heartbeat_blink = {0, 0};

static void prod_status_led_tick(uint32_t now) {
    prod_update_running_proxy(now);

    bool red = false, green = false;
    if (g_state == STATE_KILLED) {
        red = blink_pattern_tick(KILL_PATTERN, KILL_PATTERN_LEN, &g_prod_kill_blink, now, 100u);
    } else if (g_state == STATE_STARTING) {
        red = true;
        green = true; /* yellow */
    } else if (g_prod_running_proxy) {
        green = true;
    } else {
        uint16_t scale = g_batt_low ? 50u : 100u;
        bool beat = blink_pattern_tick(HEARTBEAT_PATTERN, HEARTBEAT_PATTERN_LEN, &g_prod_heartbeat_blink, now, scale);
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
 * receiver's own g_cruise_active (see receiver_firmware.c). */
static void prod_cruise_led_tick(void) {
    HAL_GPIO_WritePin(CRUISE_LED_GPIO_Port, CRUISE_LED_Pin, g_cruise_active ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void prod_app_init(void) {
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);

    g_radio.hspi     = &hspi1;
    g_radio.ce_port  = NRF_CE_GPIO_Port;
    g_radio.ce_pin   = NRF_CE_Pin;
    g_radio.csn_port = NRF_CSN_GPIO_Port;
    g_radio.csn_pin  = NRF_CSN_Pin;
    g_radio.addr     = g_unit_addr;
    nrf24_init(&g_radio);
    nrf24_enter_rx_mode(&g_radio);

    HAL_GPIO_WritePin(KILL_RELAY_GPIO_Port, KILL_RELAY_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(STARTER_RELAY_GPIO_Port, STARTER_RELAY_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(CRUISE_LED_GPIO_Port, CRUISE_LED_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(AUX1_OUT_GPIO_Port, AUX1_OUT_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(AUX2_OUT_GPIO_Port, AUX2_OUT_Pin, GPIO_PIN_RESET);
}

void prod_app_tick(void) {
    uint32_t now = HAL_GetTick();

    prod_ingestion_tick();

    /* Mirrors receiver_firmware_main()'s independent control tick exactly -
     * same calls, same order, same once-per-ms gate (that function itself
     * is never called here; this file is the real entry point, same as
     * bench_app.c is for the bench rig). */
    static uint32_t last_control_ms = 0;
    if (now != last_control_ms) {
        last_control_ms = now;
        watchdog_tick();
        start_tick();
        crank_tick();
        if (g_state != STATE_STARTING) {
            set_starter(false);
        }
        if (!g_ramping_to_idle) {
            step_toward_target(g_target_throttle);
        }
        battery_tick();
    }

    prod_actuation_tick(now);
    prod_status_led_tick(now);
    prod_cruise_led_tick();
}
