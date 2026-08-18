/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Daniel Koman */
/* ---------------------------------------------------------------------
 * bench_app.c — receiver bench rig (see DEVELOPMENT/receiver/README.md)
 *
 * Feeds the REAL receiver safety state machine from breadboard buttons and
 * a potentiometer instead of the nRF24 radio link, and drives real HAL
 * calls for the servo/display/buzzer/LEDs that the shipped
 * receiver_firmware.c only stubs out.
 *
 * src/receiver/receiver_firmware.c is pulled in below via #include and is
 * NOT modified for this rig, with one exception made upstream in that file
 * itself (not here): its millis() now calls HAL_GetTick() instead of
 * returning a hardcoded 0, because the watchdog/crank/kill-latch timing
 * this rig exists to test is meaningless against a clock that never moves.
 * That one-line fill-in is a hardware wrapper, not a safety-logic change -
 * see the safety-reviewer note in the commit that made it.
 *
 * Everything else in receiver_firmware.c - on_packet_received(),
 * handle_valid_packet(), watchdog_tick(), crank_tick(), step_toward_target(),
 * the state machine globals (g_state, g_target_throttle,
 * g_current_servo_throttle, g_starter_cooldown_until_ms, ...) - is used
 * exactly as shipped, because #include-ing the .c file puts all of it,
 * `static` or not, into this same translation unit.
 * ------------------------------------------------------------------- */

#include <string.h>
#include "bench_app.h"
#include "main.h"
#include "adc.h"
#include "tim.h"

#include "receiver_firmware.c"

/* --- Debounce (mirrors handle_firmware.c's debounce_t/debounce_update -
 * duplicated locally rather than shared, since handle_firmware.c isn't
 * touched while the transmitter bench rig is deferred). Used for start and
 * cruise; kill gets its own dedicated window below, same split as the real
 * handle. --- */
typedef struct {
    bool     raw_last;
    bool     stable;
    uint32_t last_change_ms;
} debounce_t;

static bool debounce_update(debounce_t *d, bool raw, uint32_t now) {
    if (raw != d->raw_last) {
        d->raw_last = raw;
        d->last_change_ms = now;
    }
    if (raw != d->stable && (now - d->last_change_ms) >= INPUT_DEBOUNCE_MS) {
        d->stable = raw;
    }
    return d->stable;
}

static debounce_t g_start_db;
static debounce_t g_cruise_db;

/* Kill: dedicated KILL_DEBOUNCE_MS window, mirroring the handle's
 * kill_confirmed() - kept separate from the generic debounce above because
 * kill is the one input this rig most wants to exercise faithfully. */
static bool     g_kill_btn_last = false;
static uint32_t g_kill_active_since_ms = 0;

static bool kill_button_confirmed(bool pressed, uint32_t now) {
    if (pressed && !g_kill_btn_last) {
        g_kill_active_since_ms = now;
    }
    g_kill_btn_last = pressed;
    return pressed && (now - g_kill_active_since_ms) >= KILL_DEBOUNCE_MS;
}

/* Start: mirrors start_request_confirmed() - must be held
 * START_HOLD_REQUIRED_MS before it's honored. */
static bool     g_start_btn_last = false;
static uint32_t g_start_hold_start_ms = 0;
static bool     g_start_hold_confirmed = false;

static bool start_hold_confirmed(bool pressed, uint32_t now) {
    if (pressed && !g_start_btn_last) {
        g_start_hold_start_ms = now;
        g_start_hold_confirmed = false;
    }
    if (pressed && !g_start_hold_confirmed &&
        (now - g_start_hold_start_ms) >= START_HOLD_REQUIRED_MS) {
        g_start_hold_confirmed = true;
    }
    if (!pressed) {
        g_start_hold_confirmed = false;
    }
    g_start_btn_last = pressed;
    return g_start_hold_confirmed;
}

/* --- Cruise: small bench-only mirror of apply_cruise() in
 * src/handle/handle_firmware.c. On the real system cruise is resolved
 * entirely on the handle before the packet is ever built, and the receiver
 * treats CMD_FLAG_CRUISE as transparent (see apply_aux_outputs() above).
 * Reimplemented here, not shared, because handle_firmware.c is out of
 * scope while the transmitter bench rig is deferred. --- */
static bool    g_cruise_engaged = false;
static uint8_t g_cruise_setpoint = 0;
static bool    g_cruise_btn_last = false;

/* Idle-then-retake escape state - mirrors the same block in
 * handle_firmware.c's apply_cruise(). Only meaningful while g_cruise_engaged;
 * reset whenever cruise is not engaged. */
static bool     g_cruise_idle_last = false;
static uint32_t g_cruise_idle_since_ms = 0;
static bool     g_cruise_idle_rearmed = false;

static uint8_t bench_apply_cruise(bool cruise_btn_debounced, uint8_t live_throttle, bool kill_active, uint32_t now) {
    bool rising = cruise_btn_debounced && !g_cruise_btn_last;
    g_cruise_btn_last = cruise_btn_debounced;

    if (kill_active) {
        g_cruise_engaged = false;
    } else if (rising) {
        if (!g_cruise_engaged) {
            if (live_throttle > IDLE_THRESHOLD_FOR_START) {
                g_cruise_engaged = true;
                g_cruise_setpoint = live_throttle;
            }
        } else {
            g_cruise_engaged = false;
        }
    }

    if (g_cruise_engaged &&
        (int)live_throttle > (int)g_cruise_setpoint + CRUISE_DISENGAGE_THROTTLE_DELTA) {
        g_cruise_engaged = false;
    }

    /* Idle-then-retake: see the matching comment in handle_firmware.c's
     * apply_cruise() for the full reasoning (same threshold both directions
     * so arming alone can never trigger the cancel, only a genuine
     * subsequent move can). */
    if (g_cruise_engaged) {
        bool idle_now = live_throttle <= CRUISE_REARM_THROTTLE_THRESHOLD;
        if (idle_now && !g_cruise_idle_last) {
            g_cruise_idle_since_ms = now;
            g_cruise_idle_rearmed = false;
        }
        g_cruise_idle_last = idle_now;

        if (idle_now && !g_cruise_idle_rearmed &&
            (now - g_cruise_idle_since_ms) >= CRUISE_IDLE_REARM_DELAY_MS) {
            g_cruise_idle_rearmed = true;
        }

        if (g_cruise_idle_rearmed && !idle_now) {
            g_cruise_engaged = false;
        }
    } else {
        g_cruise_idle_last = false;
        g_cruise_idle_rearmed = false;
    }

    return g_cruise_engaged ? g_cruise_setpoint : live_throttle;
}

/* --- Ingestion: buttons + pot -> a real, CRC-valid throttle_packet_t fed
 * through the actual validation path, exactly like a packet that arrived
 * over the radio would be. --- */
static uint8_t g_bench_seq = 0;

static void bench_ingestion_tick(uint32_t now) {
    bool start_raw  = (HAL_GPIO_ReadPin(START_BTN_GPIO_Port,  START_BTN_Pin)  == GPIO_PIN_RESET);
    bool cruise_raw = (HAL_GPIO_ReadPin(CRUISE_BTN_GPIO_Port, CRUISE_BTN_Pin) == GPIO_PIN_RESET);
    bool kill_raw   = (HAL_GPIO_ReadPin(KILL_BTN_GPIO_Port,   KILL_BTN_Pin)   == GPIO_PIN_RESET);

    bool start_db  = debounce_update(&g_start_db, start_raw, now);
    bool cruise_db = debounce_update(&g_cruise_db, cruise_raw, now);
    bool kill_db   = kill_button_confirmed(kill_raw, now);
    bool start_req = start_hold_confirmed(start_db, now);

    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    uint32_t raw_adc = HAL_ADC_GetValue(&hadc1);
    uint8_t live_throttle = (uint8_t)((raw_adc * 255u) / 4095u);

    uint8_t throttle = bench_apply_cruise(cruise_db, live_throttle, kill_db, now);

    throttle_packet_t pkt;
    pkt.sync = PACKET_SYNC_BYTE;
    pkt.seq = g_bench_seq++;
    pkt.throttle = throttle;
    pkt.flags = 0;
    if (kill_db) {
        pkt.flags |= CMD_FLAG_KILL;
    } else if (g_cruise_engaged) {
        pkt.flags |= CMD_FLAG_CRUISE;
    } else if (start_req) {
        pkt.flags |= CMD_FLAG_START_REQ;
    }
    pkt.crc8 = crc8_compute((const uint8_t *)&pkt, PACKET_CRC_LEN);

    uint8_t raw[PACKET_SIZE];
    memcpy(raw, &pkt, PACKET_SIZE);
    on_packet_received(raw, PACKET_SIZE);   /* real sync/CRC/sequence validation */
}

/* --- Actuation: servo PWM, 7-segment throttle readout, buzzer, LEDs. --- */

/* a,b,c,d,e,f,g truth table for 0-9 */
static const bool DIGIT_SEGMENTS[10][7] = {
    /*      a      b      c      d      e      f      g   */
    /*0*/ { true,  true,  true,  true,  true,  true,  false },
    /*1*/ { false, true,  true,  false, false, false, false },
    /*2*/ { true,  true,  false, true,  true,  false, true  },
    /*3*/ { true,  true,  true,  true,  false, false, true  },
    /*4*/ { false, true,  true,  false, false, true,  true  },
    /*5*/ { true,  false, true,  true,  false, true,  true  },
    /*6*/ { true,  false, true,  true,  true,  true,  true  },
    /*7*/ { true,  true,  true,  false, false, false, false },
    /*8*/ { true,  true,  true,  true,  true,  true,  true  },
    /*9*/ { true,  true,  true,  true,  false, true,  true  },
};

static GPIO_TypeDef *const SEG_PORTS[7] = {
    SEG_A_GPIO_Port, SEG_B_GPIO_Port, SEG_C_GPIO_Port, SEG_D_GPIO_Port,
    SEG_E_GPIO_Port, SEG_F_GPIO_Port, SEG_G_GPIO_Port
};
static const uint16_t SEG_PINS[7] = {
    SEG_A_Pin, SEG_B_Pin, SEG_C_Pin, SEG_D_Pin, SEG_E_Pin, SEG_F_Pin, SEG_G_Pin
};

static void display_show_digit(int digit, bool blank) {
    for (int i = 0; i < 7; i++) {
        bool on = !blank && digit >= 0 && digit <= 9 && DIGIT_SEGMENTS[digit][i];
#if DISPLAY_COMMON_ANODE
        GPIO_PinState level = on ? GPIO_PIN_RESET : GPIO_PIN_SET; /* sink low to light */
#else
        GPIO_PinState level = on ? GPIO_PIN_SET : GPIO_PIN_RESET; /* source high to light */
#endif
        HAL_GPIO_WritePin(SEG_PORTS[i], SEG_PINS[i], level);
    }
}

#define SERVO_PULSE_MIN_US   1000u
#define SERVO_PULSE_MAX_US   2000u
#define DISPLAY_BLINK_MS     300u
#define BENCH_BEEP_MS        80u
#define HEARTBEAT_PERIOD_MS  200u

static throttle_state_t g_bench_last_state = STATE_IDLE_SAFE;
static uint32_t g_last_seen_cooldown_until = 0;
static uint32_t g_buzzer_off_at_ms = 0;

static void bench_actuation_tick(uint32_t now) {
    /* Servo: read the already-rate-limited/ramped global, same value the
     * real firmware would hand to its own set_servo_throttle() stub. */
    uint32_t pulse_us = SERVO_PULSE_MIN_US +
        ((uint32_t)g_current_servo_throttle * (SERVO_PULSE_MAX_US - SERVO_PULSE_MIN_US)) / 255u;
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, pulse_us);

    /* 7-segment: coarse 0-9 readout of throttle, blinking while killed so a
     * "0" reading can't be mistaken for ordinary idle. */
    bool killed = (g_state == STATE_KILLED);
    bool blink_off = killed && ((now / DISPLAY_BLINK_MS) % 2 == 0);
    int digit = (int)(((uint32_t)g_current_servo_throttle * 10u) / 256u);
    display_show_digit(digit, blink_off);

    /* State-entry edges: beep + red/yellow. Green is a manual-release proxy
     * for "engine running" (see README/bench-behavior.md caveat) - it must
     * NOT light on a forced crank stop (loss-of-signal abort or the
     * MAX_CRANK_MS backstop), only a voluntary button release. Both paths
     * land in the same STATE_STARTING -> STATE_IDLE_SAFE transition, so the
     * only way to tell them apart from here is g_starter_cooldown_until_ms
     * (another static global pulled in via the #include above): end_crank()
     * only sets it on a forced stop. */
    if (g_state != g_bench_last_state) {
        if (g_state == STATE_KILLED || g_state == STATE_STARTING) {
            HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);
            g_buzzer_off_at_ms = now + BENCH_BEEP_MS;
        }
        if (g_state == STATE_KILLED || g_state == STATE_STARTING) {
            HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_RESET);
        } else if (g_bench_last_state == STATE_STARTING && g_state == STATE_IDLE_SAFE) {
            bool forced_stop = (g_starter_cooldown_until_ms != g_last_seen_cooldown_until) &&
                                ((int32_t)(g_starter_cooldown_until_ms - now) > 0);
            if (!forced_stop) {
                HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_SET);
            }
        }
        g_bench_last_state = g_state;
    }
    g_last_seen_cooldown_until = g_starter_cooldown_until_ms;

    if ((int32_t)(now - g_buzzer_off_at_ms) >= 0) {
        HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
    }

    HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin,
                       killed ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_YELLOW_GPIO_Port, LED_YELLOW_Pin,
                       (g_state == STATE_STARTING) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void bench_heartbeat_tick(uint32_t now) {
    static uint32_t last_toggle_ms = 0;
    static bool on = false;
    if ((now - last_toggle_ms) >= HEARTBEAT_PERIOD_MS) {
        last_toggle_ms = now;
        on = !on;
        HAL_GPIO_WritePin(LED_HEARTBEAT_GPIO_Port, LED_HEARTBEAT_Pin,
                           on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
}

void bench_app_init(void) {
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
    HAL_GPIO_WritePin(LED_GREEN_GPIO_Port,  LED_GREEN_Pin,  GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_RED_GPIO_Port,    LED_RED_Pin,    GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_YELLOW_GPIO_Port, LED_YELLOW_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BUZZER_GPIO_Port,     BUZZER_Pin,     GPIO_PIN_RESET);
    display_show_digit(0, false);
}

void bench_app_tick(void) {
    uint32_t now = HAL_GetTick();

    static uint32_t last_ingest_ms = 0;
    if ((now - last_ingest_ms) >= HANDLE_TX_PERIOD_MS) {
        last_ingest_ms = now;
        bench_ingestion_tick(now);
    }

    /* Mirrors receiver_firmware_main()'s independent control tick exactly -
     * same calls, same order, same once-per-ms gate. */
    static uint32_t last_control_ms = 0;
    if (now != last_control_ms) {
        last_control_ms = now;
        watchdog_tick();
        crank_tick();
        if (g_state != STATE_STARTING) {
            set_starter(false);
        }
        if (!g_ramping_to_idle) {
            step_toward_target(g_target_throttle);
        }
    }

    bench_actuation_tick(now);
    bench_heartbeat_tick(now);
}
