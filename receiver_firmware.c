/* ---------------------------------------------------------------------
 * receiver_firmware.c
 *
 * Runs on the STM32 mounted at the remote unit (near engine/throttle cable).
 * Responsibilities:
 *   - Receive + validate packets from handle
 *   - Run the kill/start/run state machine (kill always highest priority)
 *   - Drive the servo, rate-limited
 *   - Independently watch for signal loss and ramp throttle to idle
 *
 * NOTE: the mechanical kill switch is NOT handled here at all - it must
 * be wired directly into the ignition kill line in hardware, completely
 * independent of this MCU, so a software hang can't block it.
 * ------------------------------------------------------------------- */

#include <stdint.h>
#include <stdbool.h>
#include "throttle_protocol.h"
#include "crc8.h"
#include "battery_monitor.h"

typedef enum {
    STATE_IDLE_SAFE,
    STATE_RUNNING,
    STATE_STARTING,
    STATE_KILLED
} throttle_state_t;

static throttle_state_t g_state = STATE_IDLE_SAFE;

static uint8_t   g_last_accepted_seq = 0;
static bool      g_have_first_packet = false;

static uint32_t  g_last_valid_packet_ms = 0;
static bool      g_ramping_to_idle = false;
static uint32_t  g_ramp_start_ms = 0;
static uint8_t   g_ramp_start_throttle = 0;

static uint32_t  g_link_good_since_ms = 0;   /* used to enforce LINK_RESTORE_STABLE_MS after a loss event */
static bool      g_recovering_from_loss = false;

static uint8_t   g_current_servo_throttle = IDLE_THROTTLE_VALUE;
static uint8_t   g_target_throttle = IDLE_THROTTLE_VALUE;

/* Local battery monitor state (receiver pack only - no telemetry to handle). */
static uint32_t  g_batt_last_poll_ms = 0;
static bool      g_batt_low = false;

/* Receiver pack profile. Example: 2S LiFePO4 near the engine; measure real
 * full/empty/low points and the divider before trusting these numbers. */
static const battery_profile_t RX_BATT = {
    .full_mv = 7200, .empty_mv = 5000, .low_mv = 5600, .led_count = 4
};

static uint32_t millis(void) {
    // return HAL_GetTick();
    return 0; /* placeholder */
}

static void set_servo_throttle(uint8_t value) {
    // convert 0-255 to your servo's pulse width range and write it
    // e.g. HAL_TIM_PWM... or servo library call
    g_current_servo_throttle = value;
}

static void cut_ignition(void) {
    // drive the kill relay GPIO - this is the ELECTRONIC kill path,
    // separate from and in addition to the mechanical kill switch
    // HAL_GPIO_WritePin(KILL_RELAY_GPIO_Port, KILL_RELAY_Pin, GPIO_PIN_SET);
}

static void fire_starter(void) {
    // HAL_GPIO_WritePin(STARTER_RELAY_GPIO_Port, STARTER_RELAY_Pin, GPIO_PIN_SET);
    // consider a bounded pulse duration + cooldown here rather than a raw on/off
}

/* Accessory outputs (lights, smoke, ...). These are NON-safety and are
 * deliberately kept out of the kill/start/throttle state machine: they simply
 * track their command-flag levels on every valid packet, so a dropped packet
 * only delays a change by one TX period and self-heals. Cruise (CMD_FLAG_CRUISE)
 * needs no action here - the handle already froze the throttle it sent, so the
 * value flows through the normal rate-limited throttle path transparently. */
static void apply_aux_outputs(const throttle_packet_t *pkt) {
    bool aux1 = (pkt->flags & CMD_FLAG_AUX1) != 0;
    bool aux2 = (pkt->flags & CMD_FLAG_AUX2) != 0;
    // HAL_GPIO_WritePin(AUX1_OUT_GPIO_Port, AUX1_OUT_Pin, aux1 ? GPIO_PIN_SET : GPIO_PIN_RESET);
    // HAL_GPIO_WritePin(AUX2_OUT_GPIO_Port, AUX2_OUT_Pin, aux2 ? GPIO_PIN_SET : GPIO_PIN_RESET);
    (void)aux1; (void)aux2;
}

/* --- Local battery monitor (receiver side) --- identical scheme to the
 * handle, on this unit's own pack: LED bar on from power-up, piezo when low. */
static uint16_t read_battery_mv(void) {
    // uint32_t raw = HAL_ADC_GetValue(&hadc_batt);
    // return (uint16_t)(raw * ADC_MV_PER_LSB * DIVIDER_RATIO);
    return RX_BATT.full_mv; /* placeholder: pretend full */
}

static void set_battery_leds(uint8_t lit) {
    (void)lit; /* drive the bar-graph LED GPIOs: light 'lit' of RX_BATT.led_count */
}

static void set_buzzer(bool on) {
    (void)on;  /* drive the piezo GPIO (or start/stop a PWM tone) */
}

static void battery_tick(void) {
    uint32_t now = millis();
    if ((now - g_batt_last_poll_ms) >= BATTERY_POLL_MS) {
        g_batt_last_poll_ms = now;
        battery_status_t st = battery_eval(read_battery_mv(), &RX_BATT);
        set_battery_leds(st.leds_lit);
        g_batt_low = st.low;
    }
    set_buzzer(battery_buzzer_on(g_batt_low, now));
}

/* Sequence check accounting for 0-255 rollover.
 * Returns true if 'seq' is newer than 'last_seq'. */
static bool seq_is_newer(uint8_t seq, uint8_t last_seq) {
    return (uint8_t)(seq - last_seq) != 0 && (uint8_t)(seq - last_seq) < 128;
}

/* Apply rate limiting so no single update (even a valid one) can
 * slam the servo - protects against noise, glitches, and just gives
 * smoother mechanical behavior on the throttle cable. */
static void step_toward_target(uint8_t target) {
    if (g_current_servo_throttle == target) return;

    int16_t delta = (int16_t)target - (int16_t)g_current_servo_throttle;
    int16_t step = delta;
    if (step > MAX_THROTTLE_STEP_PER_TICK) step = MAX_THROTTLE_STEP_PER_TICK;
    if (step < -MAX_THROTTLE_STEP_PER_TICK) step = -MAX_THROTTLE_STEP_PER_TICK;

    set_servo_throttle((uint8_t)(g_current_servo_throttle + step));
}

/* Called whenever a packet passes sync+CRC+sequence checks. */
static void handle_valid_packet(const throttle_packet_t *pkt) {
    g_last_valid_packet_ms = millis();

    /* Track link recovery: only start counting "good link" from here,
     * reset if we were mid-loss-event. */
    if (g_recovering_from_loss) {
        if (g_link_good_since_ms == 0) {
            g_link_good_since_ms = millis();
        }
        if ((millis() - g_link_good_since_ms) >= LINK_RESTORE_STABLE_MS) {
            g_recovering_from_loss = false;
            g_link_good_since_ms = 0;
        }
    }

    /* --- 1. KILL: highest priority, checked before anything else --- */
    if (pkt->flags & CMD_FLAG_KILL) {
        cut_ignition();
        g_state = STATE_KILLED;
        g_target_throttle = IDLE_THROTTLE_VALUE;
        return; /* ignore throttle/start fields in this packet entirely */
    }

    /* KILLED is sticky - only a physical/mechanical re-arm action can
     * clear it. No wireless packet field does this. */
    if (g_state == STATE_KILLED) {
        return;
    }

    /* --- 2. START: only actionable from IDLE_SAFE, only if throttle at idle,
     *        and only if link isn't currently in a post-loss recovery window --- */
    if ((pkt->flags & CMD_FLAG_START_REQ) &&
        g_state == STATE_IDLE_SAFE &&
        pkt->throttle <= IDLE_THRESHOLD_FOR_START &&
        !g_recovering_from_loss) {
        g_state = STATE_STARTING;
        fire_starter();
        /* transition to RUNNING should happen once you have some signal the
         * engine caught (RPM sense, vibration, or just a timed window) -
         * left as a follow-up depending on what feedback you have available */
    }

    /* --- 3. THROTTLE: only apply pilot input if we're not mid-recovery --- */
    if (!g_recovering_from_loss) {
        g_target_throttle = pkt->throttle;
        if (g_state == STATE_STARTING || g_state == STATE_IDLE_SAFE) {
            /* leave as-is; state transitions to RUNNING elsewhere once
               engine-caught confirmation exists */
        }
        if (g_state != STATE_KILLED) {
            g_state = (g_target_throttle > IDLE_THRESHOLD_FOR_START) ? STATE_RUNNING : g_state;
        }
    }

    g_ramping_to_idle = false; /* fresh valid data cancels any in-progress ramp */
}

/* Called once per packet received, before acting on contents. */
static void on_packet_received(const uint8_t *raw, uint8_t len) {
    if (len != PACKET_SIZE) return;

    const throttle_packet_t *pkt = (const throttle_packet_t *)raw;

    if (pkt->sync != PACKET_SYNC_BYTE) return;

    uint8_t computed_crc = crc8_compute(raw, PACKET_CRC_LEN);
    if (computed_crc != pkt->crc8) return; /* corrupted - discard entirely */

    if (g_have_first_packet && !seq_is_newer(pkt->seq, g_last_accepted_seq)) {
        return; /* stale or duplicate - discard */
    }

    g_last_accepted_seq = pkt->seq;
    g_have_first_packet = true;

    handle_valid_packet(pkt);      /* safety state machine first (kill/start/throttle) */
    apply_aux_outputs(pkt);        /* then non-safety accessories, always refreshed */
}

/* Runs independently of packet reception - this is what protects you
 * when packets simply stop arriving at all. */
static void watchdog_tick(void) {
    if (g_state == STATE_KILLED) return; /* already safe, nothing to ramp */
    if (!g_have_first_packet) return;    /* haven't linked yet, nothing to lose */

    uint32_t since_last = millis() - g_last_valid_packet_ms;

    if (since_last >= WATCHDOG_FULL_IDLE_MS) {
        /* fully committed to idle, hold, mark link as lost until it
         * proves stable again for LINK_RESTORE_STABLE_MS */
        g_target_throttle = IDLE_THROTTLE_VALUE;
        set_servo_throttle(IDLE_THROTTLE_VALUE);
        g_ramping_to_idle = false;
        g_recovering_from_loss = true;
        g_link_good_since_ms = 0;
        return;
    }

    if (since_last >= WATCHDOG_RAMP_START_MS) {
        if (!g_ramping_to_idle) {
            g_ramping_to_idle = true;
            g_ramp_start_ms = millis();
            g_ramp_start_throttle = g_current_servo_throttle;
        }
        uint32_t elapsed = millis() - g_ramp_start_ms;
        if (elapsed >= RAMP_TO_IDLE_DURATION_MS) {
            set_servo_throttle(IDLE_THROTTLE_VALUE);
        } else {
            /* linear ramp from ramp_start_throttle down to idle over
               RAMP_TO_IDLE_DURATION_MS - fixed, not tunable in flight */
            uint32_t remaining_frac_num = (RAMP_TO_IDLE_DURATION_MS - elapsed);
            uint8_t value = (uint8_t)((uint32_t)g_ramp_start_throttle * remaining_frac_num
                                       / RAMP_TO_IDLE_DURATION_MS);
            set_servo_throttle(value);
        }
    }
}

int receiver_firmware_main(void) {
    /* --- Init section (fill in) ---
     * HAL_Init(); SystemClock_Config(); MX_GPIO_Init(); MX_TIM_PWM_Init();
     * radio.begin();
     * radio.setPALevel(RF24_PA_HIGH);
     * radio.setDataRate(RF24_250KBPS);
     * radio.setChannel(...);          // must match handle
     * radio.setAutoAck(false);
     * radio.startListening();
     */

    uint32_t last_tick = millis();

    while (1) {
        /* if (radio.available()) {
         *     uint8_t buf[PACKET_SIZE];
         *     radio.read(buf, PACKET_SIZE);
         *     on_packet_received(buf, PACKET_SIZE);
         * }
         */

        uint32_t now = millis();
        if (now != last_tick) { /* run watchdog + servo step roughly once per ms tick, adjust to your loop rate */
            last_tick = now;
            watchdog_tick();
            if (!g_ramping_to_idle && g_state != STATE_KILLED) {
                step_toward_target(g_target_throttle);
            }
            battery_tick(); /* independent of packet arrival; non-blocking */
        }
    }
}
