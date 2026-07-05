/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Daniel Koman */
#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <stdint.h>
#include <stdbool.h>

/* ---------------------------------------------------------------------
 * Local battery monitor - runs INDEPENDENTLY on both the transmitter and
 * the receiver. There is deliberately NO return telemetry: each unit only
 * ever looks at its own pack. That keeps the link one-way (handle -> remote)
 * and avoids the complexity of the handle having to receive and display the
 * receiver's battery level.
 *
 * Only the pure math lives here (millivolts -> LED bar count + low flag) so
 * it can be hand-verified/unit-tested off-target. The ADC read, the bar
 * LEDs, and the piezo buzzer are wired PER BOARD in each firmware, because
 * the two units use different packs, dividers, and pins.
 * ------------------------------------------------------------------- */

/* Buzzer cadence for the low-battery alert (both sides). Non-blocking:
 * derive the on/off state from the millisecond clock, never delay(). */
#define BATTERY_POLL_MS          500    /* how often to re-read the pack + refresh LEDs */
#define BATTERY_BUZZ_ON_MS       120    /* buzzer on-time within each beep */
#define BATTERY_BUZZ_PERIOD_MS   2000   /* one short beep per this period while low */

typedef struct {
    uint16_t full_mv;    /* at/above this  -> all bars lit */
    uint16_t empty_mv;   /* at/below this  -> 0 bars */
    uint16_t low_mv;     /* at/below this  -> low flag set (buzzer) */
    uint8_t  led_count;  /* number of bar LEDs, 3 or 4 */
} battery_profile_t;

typedef struct {
    uint8_t leds_lit;    /* 0 .. led_count */
    bool    low;         /* true -> sound the buzzer */
} battery_status_t;

/* Map a measured pack voltage (millivolts, already scaled back up from the
 * divider) to a bar count + low flag. Rounds to the nearest bar, and never
 * shows 0 bars while there is still charge above empty (so "1 bar + buzzer"
 * is the low-but-not-dead indication). */
static inline battery_status_t battery_eval(uint16_t mv, const battery_profile_t *p) {
    battery_status_t s;
    s.low = (mv <= p->low_mv);

    if (mv >= p->full_mv) {
        s.leds_lit = p->led_count;
    } else if (mv <= p->empty_mv) {
        s.leds_lit = 0;
    } else {
        uint32_t span  = (uint32_t)(p->full_mv - p->empty_mv);
        uint32_t above = (uint32_t)(mv - p->empty_mv);
        s.leds_lit = (uint8_t)((above * p->led_count + span / 2) / span); /* round to nearest */
        if (s.leds_lit == 0) s.leds_lit = 1; /* still some charge -> show at least one */
    }
    return s;
}

/* Pure buzzer gate: returns whether the piezo should be ON right now.
 * Off entirely unless 'low'; otherwise a short beep once per period.
 * 'now_ms' is a free-running millisecond counter (e.g. HAL_GetTick()). */
static inline bool battery_buzzer_on(bool low, uint32_t now_ms) {
    if (!low) return false;
    return (now_ms % BATTERY_BUZZ_PERIOD_MS) < BATTERY_BUZZ_ON_MS;
}

#endif /* BATTERY_MONITOR_H */
