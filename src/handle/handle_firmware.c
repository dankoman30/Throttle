/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Daniel Koman */
/* ---------------------------------------------------------------------
 * handle_firmware.c
 *
 * Runs on the STM32 mounted in the trigger handle.
 * Responsibilities:
 *   - Read trigger position (ADC), kill switch, start button
 *   - Debounce buttons, require hold-to-start
 *   - Build a throttle_packet_t and transmit over nRF24L01+ at a fixed rate
 *
 * NOTE: HAL/RF24 calls below are named to match typical STM32 HAL +
 * RF24 (TMRh20) library conventions, but are stubbed/commented -
 * wire these to your actual peripheral init once board/pins are chosen.
 * ------------------------------------------------------------------- */

#include <stdint.h>
#include <stdbool.h>
#include "throttle_protocol.h"
#include "crc8.h"
#ifdef USE_HAL_DRIVER
#include "stm32l4xx_hal.h" /* HAL_GetTick() for millis() below */
#endif

/* Real ADC/GPIO reads and the nRF24 TX itself live in
 * DEVELOPMENT/handle/handle-prod/Src/handle_app.c, which #includes this
 * file with HANDLE_PROD_BOARD defined - see that file's header comment for
 * the exact list of guarded exceptions made in this one. */

/* Generic time-based debounce for momentary/rocker inputs: the raw reading must
 * stay stable for INPUT_DEBOUNCE_MS before the debounced state changes, rejecting
 * contact bounce and vibration glitches. Used for the cruise + accessory inputs;
 * the kill line has its own dedicated debounce (kill_confirmed) because it is
 * fail-safe/latching. */
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

/* --- Local state --- */
static uint8_t   g_seq = 0;
static bool      g_start_button_pressed_last = false;
static uint32_t  g_start_hold_start_ms = 0;
static bool      g_start_hold_confirmed = false;

/* Kill is LATCHED on the handle: once the kill switch is seen active even
 * once, we keep commanding kill on every packet from then on. A brief press
 * therefore produces a sustained stream of kill packets rather than a few,
 * so a run of dropped packets (no radio ack in this design) can't swallow it.
 * This mirrors the receiver's sticky STATE_KILLED. Clearing it requires a
 * deliberate re-arm - a power cycle here, which matches the "physical re-arm
 * only" rule in PROJECT_DESIGN.md. The mechanical kill line is the backup for
 * when the radio itself is dead. */
static bool      g_kill_latched = false;

/* Debounce state for the fail-safe kill line (see kill_confirmed). */
static bool      g_kill_active_last = false;
static uint32_t  g_kill_active_since_ms = 0;

/* Cruise control state (handle-side; see apply_cruise). */
static bool      g_cruise_engaged = false;
static uint8_t   g_cruise_setpoint = 0;
static bool      g_cruise_button_last = false;

/* Idle-then-retake escape state (see apply_cruise). Only meaningful while
 * g_cruise_engaged; reset whenever cruise is not engaged. */
static bool      g_cruise_idle_last = false;    /* was throttle at/below the rearm threshold last tick */
static uint32_t  g_cruise_idle_since_ms = 0;     /* when throttle most recently arrived at/below it */
static bool      g_cruise_idle_rearmed = false;  /* continuous hold satisfied; next real move cancels cruise */

/* Debounce state for the cruise button and the two accessory inputs. */
static debounce_t g_cruise_btn_db;
static debounce_t g_aux1_db;
static debounce_t g_aux2_db;

/* AUX1 (e.g. strobe lights) is LATCHED here, same toggle-on-rising-edge
 * shape as cruise below: press once to turn on, press again to turn off.
 * g_aux1_engaged is exposed for a real board's indicator LED, same
 * externally-observed-state pattern as everything else in this file. */
static bool      g_aux1_engaged = false;
static bool      g_aux1_button_last = false;

/* AUX2 (e.g. smoke) is purely momentary - a live debounced level, on only
 * while the switch is held/closed. g_aux2_state is exposed the same way. */
static bool      g_aux2_state = false;

static uint32_t millis(void) {
#ifdef USE_HAL_DRIVER
    return HAL_GetTick();
#else
    return 0; /* placeholder: no HAL in the host-side compile-check build */
#endif
}

/* Takes TRIGGER_OVERSAMPLE_COUNT raw ADC conversions back-to-back and
 * averages them into one sample. This is the anti-alias half of the
 * pipeline - it has to happen at the sampling stage itself (before
 * anything below sees a single, already-aliased value), which is exactly
 * what the EMA/deadband below can't do after the fact. Negligibly fast
 * relative to HANDLE_TX_PERIOD_MS even at a lengthened ADC sample time
 * (see docs/OPEN-ITEMS.md "Trigger-ADC anti-alias + oversampling") - a
 * handful of back-to-back 12-bit conversions is microseconds, not
 * milliseconds. */
static uint32_t read_throttle_raw_oversampled(void) {
#ifdef HANDLE_PROD_BOARD
    uint32_t sum = 0;
    for (uint32_t i = 0; i < TRIGGER_OVERSAMPLE_COUNT; i++) {
        HAL_ADC_Start(&hadc1);
        HAL_ADC_PollForConversion(&hadc1, 10);
        sum += HAL_ADC_GetValue(&hadc1); /* 0-4095, 12-bit */
    }
    return sum / TRIGGER_OVERSAMPLE_COUNT;
#else
    return 0; /* placeholder */
#endif
}

/* Read raw ADC and map to 0-255 throttle scale.
 * Apply a light low-pass filter here so noisy ADC readings don't
 * translate into a jittery servo on the receiving end.
 *
 * FAIL-SAFE WIRING: put a pull-down on the ADC input (and reference the
 * trigger pot so its wiper idles toward 0), so a broken/disconnected trigger
 * reads ~0 = idle, never a spurious high throttle. This is the analog
 * equivalent of the fail-safe switch polarity: the failure state is the
 * safe state. */
static uint8_t read_throttle_position(void) {
    uint32_t raw = read_throttle_raw_oversampled();

    static uint32_t filtered = 0;
    const uint32_t FILTER_SHIFT = 1; /* simple exponential moving average, tune as needed */
    filtered = filtered - (filtered >> FILTER_SHIFT) + raw;
    uint32_t smoothed = filtered >> FILTER_SHIFT;

    /* map 0-4095 -> 0-255; adjust min/max to your actual trigger travel
     * (measure real ADC min/max at full release / full pull, don't assume rails) */
    uint8_t mapped = (uint8_t)((smoothed * 255) / 4095);

    /* Deadband / hysteresis: hold the last transmitted value unless the trigger
     * moved at least THROTTLE_DEADBAND, so hand/engine vibration doesn't make the
     * servo hunt. Each servo position thus owns a band of trigger values. The
     * rails (0, 255) always update so full idle and full throttle stay exactly
     * reachable. Complements the EMA above and the receiver's rate-limiter. */
    static uint8_t last_out = 0;
    int diff = (int)mapped - (int)last_out;
    if (diff < 0) diff = -diff;
    if (mapped == 0 || mapped == 255 || diff >= THROTTLE_DEADBAND) {
        last_out = mapped;
    }
    return last_out;
}

/* Returns true when kill is being REQUESTED. Wired FAIL-SAFE: the kill switch
 * is normally-closed with a pull-up, so during normal operation the pin is
 * held LOW ("armed/ok"); pressing kill OPENS the switch and, crucially, so
 * does a broken wire or a vibrated-loose connector - both let the pull-up
 * pull the pin HIGH = kill. The failure state is the safe state.
 * (This is the reverse polarity from start/cruise/aux, and that is on purpose:
 *  a missed kill is dangerous, a spurious kill is merely a safe engine-off.) */
static bool read_kill_switch(void) {
#ifdef HANDLE_PROD_BOARD
    return HAL_GPIO_ReadPin(KILL_SW_GPIO_Port, KILL_SW_Pin) == GPIO_PIN_SET; /* open/broken = HIGH = kill */
#else
    return false; /* placeholder: "not requesting kill" so the stub stays runnable */
#endif
}

/* Debounced kill: the line must read "kill" continuously for KILL_DEBOUNCE_MS
 * before we treat it as a real kill. This rejects brief vibration glitches on
 * the normally-closed line without weakening fail-safe - a genuine press or a
 * truly severed wire stays open and easily outlasts the window. Latching
 * itself (g_kill_latched) is handled by the caller and is permanent until
 * re-arm; this only gates the moment of latching. */
static bool kill_confirmed(void) {
    bool active = read_kill_switch();
    if (active && !g_kill_active_last) {
        g_kill_active_since_ms = millis();   /* transition into "kill requested" */
    }
    g_kill_active_last = active;
    return active && (millis() - g_kill_active_since_ms) >= KILL_DEBOUNCE_MS;
}

static bool read_start_button_raw(void) {
#ifdef HANDLE_PROD_BOARD
    return HAL_GPIO_ReadPin(START_BTN_GPIO_Port, START_BTN_Pin) == GPIO_PIN_RESET; /* pull-up, active-low */
#else
    return false; /* placeholder */
#endif
}

/* --- Cruise + accessory inputs ---
 * Each returns the LOGICAL "active/requested" state, hiding electrical
 * polarity. Everything here EXCEPT kill is wired the same, intuitive way:
 * closed = on/active, with a pull-down so an open/broken wire reads off - the
 * safe state for these is "do nothing" (don't crank, don't engage cruise,
 * accessory off). Only kill inverts (see read_kill_switch). Keeping the ONE
 * polarity decision per input right here makes re-wiring a one-line change. */
static bool read_cruise_button_raw(void) { /* closed = pressed */
#ifdef HANDLE_PROD_BOARD
    return HAL_GPIO_ReadPin(CRUISE_BTN_GPIO_Port, CRUISE_BTN_Pin) == GPIO_PIN_SET; /* pull-down */
#else
    return false; /* placeholder */
#endif
}

static bool read_aux1_switch(void) { /* momentary button; closed = pressed. Latched into a toggle in build_packet(). */
#ifdef HANDLE_PROD_BOARD
    return HAL_GPIO_ReadPin(AUX1_SW_GPIO_Port, AUX1_SW_Pin) == GPIO_PIN_SET; /* pull-down */
#else
    return false; /* placeholder */
#endif
}

static bool read_aux2_switch(void) { /* e.g. smoke: closed = on, purely momentary (no latch) */
#ifdef HANDLE_PROD_BOARD
    return HAL_GPIO_ReadPin(AUX2_SW_GPIO_Port, AUX2_SW_Pin) == GPIO_PIN_SET; /* pull-down */
#else
    return false; /* placeholder */
#endif
}

/* Cruise control, resolved entirely on the handle.
 *   - Rising edge of the cruise button toggles cruise on/off.
 *   - Engaging captures the current trigger position as the setpoint; while
 *     engaged we transmit that frozen value instead of the live trigger, so
 *     the pilot can release the trigger and the throttle holds.
 *   - Cruise drops on any of three independent paths:
 *       1. kill (always), or a second button press.
 *       2. the pilot pulling the trigger ABOVE the setpoint by more than
 *          CRUISE_DISENGAGE_THROTTLE_DELTA (an override to take manual
 *          control / accelerate).
 *       3. idle-then-retake: the trigger held continuously at/below
 *          CRUISE_REARM_THROTTLE_THRESHOLD for CRUISE_IDLE_REARM_DELAY_MS,
 *          followed by any move back above that threshold. Lets the pilot
 *          simply let go and retake the trigger to get manual control back,
 *          without pulling past the setpoint or hunting for the cruise
 *          button. The 2s continuous-hold requirement (any dip back above
 *          the threshold during the wait resets it) distinguishes a
 *          deliberate release from a brief dip, and reusing the same
 *          threshold both directions means the hold timer can only ever
 *          finish while throttle is still at/below it - so becoming armed
 *          can never by itself trigger the cancel, only a genuine
 *          subsequent move can.
 *     Merely releasing the trigger below the setpoint and holding it there
 *     indefinitely is exactly what cruise is for and does NOT disengage on
 *     its own - path 3 only fires on the pilot's next actual input after the
 *     hold, not from resting at idle.
 * Returns the throttle value that should actually be transmitted this packet.
 * Kill must be evaluated (g_kill_latched set) BEFORE calling this. */
static uint8_t apply_cruise(uint8_t live_throttle, uint32_t now) {
    bool btn = debounce_update(&g_cruise_btn_db, read_cruise_button_raw(), now);
    bool rising = btn && !g_cruise_button_last;
    g_cruise_button_last = btn;

    if (g_kill_latched) {
        g_cruise_engaged = false;           /* kill always disengages cruise */
    } else if (rising) {
        if (!g_cruise_engaged) {
            /* only engage if we're actually above idle - holding "idle" is
             * pointless and could surprise the pilot */
            if (live_throttle > IDLE_THRESHOLD_FOR_START) {
                g_cruise_engaged = true;
                g_cruise_setpoint = live_throttle;
            }
        } else {
            g_cruise_engaged = false;       /* second press = toggle off */
        }
    }

    if (g_cruise_engaged) {
        /* upward override only; use int math so setpoint+delta can't wrap */
        if ((int)live_throttle > (int)g_cruise_setpoint + CRUISE_DISENGAGE_THROTTLE_DELTA) {
            g_cruise_engaged = false;       /* pilot pulled past hold point -> manual */
        }
    }

    if (g_cruise_engaged) {
        bool idle_now = live_throttle <= CRUISE_REARM_THROTTLE_THRESHOLD;
        if (idle_now && !g_cruise_idle_last) {
            g_cruise_idle_since_ms = now;   /* just arrived at neutral - (re)start the hold timer */
            g_cruise_idle_rearmed = false;
        }
        g_cruise_idle_last = idle_now;

        if (idle_now && !g_cruise_idle_rearmed &&
            (now - g_cruise_idle_since_ms) >= CRUISE_IDLE_REARM_DELAY_MS) {
            g_cruise_idle_rearmed = true;
        }

        if (g_cruise_idle_rearmed && !idle_now) {
            g_cruise_engaged = false;       /* pilot retook the trigger -> manual */
        }
    } else {
        g_cruise_idle_last = false;
        g_cruise_idle_rearmed = false;
    }

    return g_cruise_engaged ? g_cruise_setpoint : live_throttle;
}

/* Requires the start button to be held continuously for
 * START_HOLD_REQUIRED_MS before it's considered a valid request.
 * This guards against a brief bump or RF glitch being read as start intent
 * further down the chain (receiver still re-validates independently). */
static bool start_request_confirmed(void) {
    bool pressed = read_start_button_raw();

    if (pressed && !g_start_button_pressed_last) {
        /* rising edge - begin hold timer */
        g_start_hold_start_ms = millis();
        g_start_hold_confirmed = false;
    }

    if (pressed && !g_start_hold_confirmed) {
        if ((millis() - g_start_hold_start_ms) >= START_HOLD_REQUIRED_MS) {
            g_start_hold_confirmed = true;
        }
    }

    if (!pressed) {
        g_start_hold_confirmed = false;
    }

    g_start_button_pressed_last = pressed;
    return g_start_hold_confirmed;
}

/* Most recently built packet. A real board's driver reads this and sends it
 * over the radio externally - same externally-observed-state pattern
 * receiver_firmware.c uses for its outputs (g_current_servo_throttle,
 * g_aux1_state, ...), just applied to an output that happens to be built
 * inside this shared file instead of a bare stub. */
static throttle_packet_t g_last_packet;

static void build_packet(void) {
    uint32_t now = millis();
    throttle_packet_t pkt;
    pkt.sync = PACKET_SYNC_BYTE;
    pkt.seq = g_seq++;

    /* Evaluate kill FIRST so cruise sees it, then let cruise decide what
     * throttle value actually goes on the wire (frozen setpoint vs live). */
    if (kill_confirmed()) {
        g_kill_latched = true;   /* sticky: never un-latches without a re-arm */
    }
    pkt.throttle = apply_cruise(read_throttle_position(), now);

    pkt.flags = 0;
    if (g_kill_latched) {
        /* Once killed, suppress start AND cruise so a latched kill can never
         * share a packet with them. Cruise is already force-disengaged above. */
        pkt.flags |= CMD_FLAG_KILL;
    } else if (g_cruise_engaged) {
        pkt.flags |= CMD_FLAG_CRUISE;
    } else if (start_request_confirmed()) {
        pkt.flags |= CMD_FLAG_START_REQ;
    }

    /* Accessories are independent of the primary command, including kill -
     * same as before, just one is now latched and one is momentary.
     * AUX1: toggle on a rising edge of the debounced button. */
    bool aux1_btn = debounce_update(&g_aux1_db, read_aux1_switch(), now);
    if (aux1_btn && !g_aux1_button_last) {
        g_aux1_engaged = !g_aux1_engaged;
    }
    g_aux1_button_last = aux1_btn;
    if (g_aux1_engaged) pkt.flags |= CMD_FLAG_AUX1;

    /* AUX2: live debounced level, no latch. */
    g_aux2_state = debounce_update(&g_aux2_db, read_aux2_switch(), now);
    if (g_aux2_state) pkt.flags |= CMD_FLAG_AUX2;

    pkt.crc8 = crc8_compute((const uint8_t *)&pkt, PACKET_CRC_LEN);

    g_last_packet = pkt;
}

int handle_firmware_main(void) {
    /* --- Init section (fill in) ---
     * HAL_Init();
     * SystemClock_Config();
     * MX_ADC1_Init();
     * MX_GPIO_Init();
     * MX_SPI1_Init();                      // + assign g_radio's hspi/CE/CSN pins here
     * nrf24_init(&g_radio);                 // EN_AA off (ADR 0001), PA_HIGH, static payload -
     *                                       // see src/common/nrf24.h. Channel/address are still
     *                                       // bring-up placeholders (docs/OPEN-ITEMS.md "Channel
     *                                       // selection"), and RF_SETUP defaults to 1Mbps as a
     *                                       // bring-up diagnostic finding, not a final data-rate
     *                                       // decision - see docs/decisions/0005-radio-choice-
     *                                       // and-ignition-emi.md's 2026-08-06 addendum.
     *                                       // nrf24_init() already leaves the chip ready to
     *                                       // transmit (PTX, CE low) - handle needs no further
     *                                       // mode call.
     *
     * This stub loop is never actually called on real hardware - see
     * DEVELOPMENT/handle/handle-prod/Src/handle_app.c, which drives
     * build_packet() and the radio send itself. Kept here as a host-side
     * reference/compile-check entry point only.
     */

    uint32_t last_tx = millis();

    while (1) {
        uint32_t now = millis();
        if ((now - last_tx) >= HANDLE_TX_PERIOD_MS) {
            last_tx = now;
            build_packet();
            // nrf24_send_payload(&g_radio, (const uint8_t *)&g_last_packet, PACKET_SIZE);
        }
        /* keep loop tight - avoid blocking delays here, timing precision matters */
    }
}
