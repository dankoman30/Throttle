/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Daniel Koman */
#ifndef LED_BLINK_H
#define LED_BLINK_H

#include <stdint.h>
#include <stdbool.h>

/* ---------------------------------------------------------------------
 * Shared status-LED blink patterns + sequencer, used identically by both
 * boards (see DEVELOPMENT/receiver/receiver-prod/Src/prod_app.c and
 * DEVELOPMENT/handle/handle-prod/Src/handle_app.c) so their red channel
 * means the same thing on both ends:
 *   HEARTBEAT - "armed, never yet started" (or freshly re-armed after
 *               kill). Lub-dub: two quick pulses, then a longer pause.
 *   KILL      - overrides the heartbeat once latched. Long-long: two
 *               slower, more deliberate pulses.
 * Heartbeat ~2500ms/cycle, kill ~1000ms/cycle - tune like any other timing
 * constant in this project, on the bench.
 * ------------------------------------------------------------------- */

typedef struct {
    bool     on;
    uint16_t duration_ms;
} blink_step_t;

static const blink_step_t HEARTBEAT_PATTERN[] = {
    { true,  125 },
    { false, 150 },
    { true,  125 },
    { false, 2100 },
};
#define HEARTBEAT_PATTERN_LEN (sizeof(HEARTBEAT_PATTERN) / sizeof(HEARTBEAT_PATTERN[0]))

static const blink_step_t KILL_PATTERN[] = {
    { true,  500 },
    { false, 500 },
};
#define KILL_PATTERN_LEN (sizeof(KILL_PATTERN) / sizeof(KILL_PATTERN[0]))

/* Per-instance cursor state - same idiom as handle_firmware.c's debounce_t.
 * One instance per independently-running pattern (e.g. the receiver's
 * heartbeat and kill patterns never run at the same time, but each still
 * gets its own state so resuming one never inherits the other's timing). */
typedef struct {
    uint32_t step_start_ms;
    uint8_t  step_idx;
} blink_state_t;

/* Cycles 'state' through 'pattern' (length 'len'), looping back to the
 * start. Returns whether the LED should be ON right now. */
static inline bool blink_pattern_tick(const blink_step_t *pattern, uint8_t len,
                                       blink_state_t *state, uint32_t now) {
    if ((now - state->step_start_ms) >= pattern[state->step_idx].duration_ms) {
        state->step_start_ms = now;
        state->step_idx = (uint8_t)((state->step_idx + 1) % len);
    }
    return pattern[state->step_idx].on;
}

#endif /* LED_BLINK_H */
