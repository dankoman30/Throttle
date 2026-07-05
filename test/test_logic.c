/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Daniel Koman */
/* ---------------------------------------------------------------------
 * test_logic.c - host-side sanity tests for the PURE logic pieces.
 * No hardware, no HAL. Build and run on your PC:
 *
 *     gcc -Wall -Wextra -I. test_logic.c -o test_logic && ./test_logic
 *
 * Covers: CRC8 known vector, sequence rollover, battery bar mapping,
 * low-battery buzzer cadence, and the cruise engage/disengage rules.
 * The cruise + seq functions are static inside the firmware .c files, so
 * the exact logic is mirrored here (kept in lockstep by hand) to test it
 * without dragging in the HAL stubs.
 * ------------------------------------------------------------------- */

#include <stdio.h>
#include <string.h>
#include "crc8.h"
#include "battery_monitor.h"
#include "throttle_protocol.h"

static int g_fail = 0;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL: %s (line %d)\n", #cond, __LINE__); g_fail++; } \
} while (0)

/* --- mirror of receiver_firmware.c:seq_is_newer --- */
static bool seq_is_newer(uint8_t seq, uint8_t last_seq) {
    return (uint8_t)(seq - last_seq) != 0 && (uint8_t)(seq - last_seq) < 128;
}

/* --- mirror of handle_firmware.c:kill_confirmed (pure form) ---
 * 'active' = read_kill_switch() result (true = kill requested). */
typedef struct { bool active_last; uint32_t since; } killdb_t;
static bool kill_confirmed_m(killdb_t *k, bool active, uint32_t now) {
    if (active && !k->active_last) k->since = now;
    k->active_last = active;
    return active && (now - k->since) >= KILL_DEBOUNCE_MS;
}

/* --- mirror of handle_firmware.c:apply_cruise (pure form) ---
 * State passed explicitly instead of via file statics. */
typedef struct { bool engaged; uint8_t setpoint; bool btn_last; bool kill; } cruise_t;

static uint8_t cruise_step(cruise_t *c, bool btn, uint8_t live) {
    bool rising = btn && !c->btn_last;
    c->btn_last = btn;

    if (c->kill) {
        c->engaged = false;
    } else if (rising) {
        if (!c->engaged) {
            if (live > IDLE_THRESHOLD_FOR_START) { c->engaged = true; c->setpoint = live; }
        } else {
            c->engaged = false;
        }
    }
    if (c->engaged) {
        if ((int)live > (int)c->setpoint + CRUISE_DISENGAGE_THROTTLE_DELTA) c->engaged = false;
    }
    return c->engaged ? c->setpoint : live;
}

/* --- mirror of receiver_firmware.c:rpm_from_interval_ms --- */
static uint16_t rpm_from_interval_ms(uint32_t interval_ms) {
    if (interval_ms == 0) return 0;
    uint32_t rpm = 60000u / interval_ms;
    return (rpm > 0xFFFFu) ? 0xFFFFu : (uint16_t)rpm;
}

/* --- mirror of receiver_firmware.c:engine_caught_tick (pure form) ---
 * State passed explicitly; rpm is driven as an input. */
typedef enum { S_IDLE, S_RUN, S_START } st_t;
typedef struct {
    st_t     state;
    uint16_t rpm;
    uint32_t crank_start;
    uint32_t caught_since;
    bool     recovering;
    bool     starter_on;
} ecu_t;

static void ecu_tick(ecu_t *e, uint32_t now) {
    if (e->state != S_START) return;
    if (e->recovering) { e->starter_on = false; e->state = S_IDLE; return; }
    if (e->rpm >= RPM_CAUGHT_THRESHOLD) {
        if (e->caught_since == 0) e->caught_since = now;
        if ((now - e->caught_since) >= RPM_CAUGHT_STABLE_MS) {
            e->starter_on = false; e->state = S_RUN; return;
        }
    } else {
        e->caught_since = 0;
    }
    if ((now - e->crank_start) >= CRANK_TIMEOUT_MS) {
        e->starter_on = false; e->state = S_IDLE;
    }
}

int main(void) {
    /* --- CRC8/MAXIM known-answer vector --- */
    CHECK(crc8_compute((const uint8_t *)"123456789", 9) == 0xA1);

    /* --- sequence rollover --- */
    CHECK(seq_is_newer(1, 0));
    CHECK(seq_is_newer(0, 255));          /* wrap forward */
    CHECK(!seq_is_newer(0, 0));           /* duplicate is not newer */
    CHECK(!seq_is_newer(200, 5));         /* >127 ahead treated as old/reordered */
    CHECK(seq_is_newer(5, 200));          /* small forward wrap */

    /* --- battery bar mapping (4-LED handle profile) --- */
    battery_profile_t p = { .full_mv = 8400, .empty_mv = 6000, .low_mv = 6600, .led_count = 4 };
    CHECK(battery_eval(9000, &p).leds_lit == 4);   /* above full clamps */
    CHECK(battery_eval(8400, &p).leds_lit == 4);
    CHECK(battery_eval(5000, &p).leds_lit == 0);   /* below empty */
    CHECK(battery_eval(6000, &p).leds_lit == 0);   /* exactly empty */
    CHECK(!battery_eval(8400, &p).low);
    CHECK(battery_eval(6600, &p).low);             /* at low threshold -> low */
    CHECK(battery_eval(6500, &p).low);
    /* just above empty but still charged -> at least one bar, and low flag set */
    { battery_status_t s = battery_eval(6100, &p); CHECK(s.leds_lit >= 1 && s.low); }

    /* --- buzzer cadence: off unless low, then short beep once per period --- */
    CHECK(!battery_buzzer_on(false, 0));
    CHECK(!battery_buzzer_on(false, 100));
    CHECK(battery_buzzer_on(true, 0));                              /* start of beep */
    CHECK(battery_buzzer_on(true, BATTERY_BUZZ_ON_MS - 1));
    CHECK(!battery_buzzer_on(true, BATTERY_BUZZ_ON_MS));            /* beep ended */
    CHECK(!battery_buzzer_on(true, BATTERY_BUZZ_PERIOD_MS - 1));
    CHECK(battery_buzzer_on(true, BATTERY_BUZZ_PERIOD_MS));         /* next period */

    /* --- cruise: engage holds setpoint through trigger RELEASE (the point) --- */
    { cruise_t c = {0};
      CHECK(cruise_step(&c, false, 120) == 120);   /* no button: passthrough */
      CHECK(cruise_step(&c, true, 120) == 120);    /* press engages at 120 */
      CHECK(c.engaged && c.setpoint == 120);
      CHECK(cruise_step(&c, false, 0) == 120);     /* trigger fully released -> still 120 */
      CHECK(cruise_step(&c, false, 60) == 120);    /* partial squeeze below hold -> still 120 */
      CHECK(cruise_step(&c, false, 125) == 120);   /* small pull within delta -> held */
      CHECK(c.engaged);
    }
    /* --- cruise: pulling ABOVE setpoint+delta overrides to manual, follows live --- */
    { cruise_t c = {0};
      cruise_step(&c, true, 120);                  /* engage @120 */
      uint8_t out = cruise_step(&c, false, 120 + CRUISE_DISENGAGE_THROTTLE_DELTA + 1);
      CHECK(!c.engaged && out == 120 + CRUISE_DISENGAGE_THROTTLE_DELTA + 1);
    }
    /* --- cruise: second press toggles off --- */
    { cruise_t c = {0};
      cruise_step(&c, true, 120); CHECK(c.engaged);
      cruise_step(&c, false, 120);                 /* release button (no edge) */
      cruise_step(&c, true, 120); CHECK(!c.engaged); /* second press disengages */
    }
    /* --- cruise: kill always disengages, and won't engage at/below idle --- */
    { cruise_t c = {0}; c.kill = true;
      cruise_step(&c, true, 120); CHECK(!c.engaged);
    }
    { cruise_t c = {0};
      cruise_step(&c, true, IDLE_THRESHOLD_FOR_START); CHECK(!c.engaged); /* too low to engage */
    }

    /* --- kill debounce: must persist KILL_DEBOUNCE_MS before confirming --- */
    { killdb_t k = {0};
      CHECK(!kill_confirmed_m(&k, true, 0));                    /* just went active */
      CHECK(!kill_confirmed_m(&k, true, KILL_DEBOUNCE_MS - 1)); /* not long enough */
      CHECK(kill_confirmed_m(&k, true, KILL_DEBOUNCE_MS));      /* held long enough */
      CHECK(kill_confirmed_m(&k, true, KILL_DEBOUNCE_MS + 500));/* stays confirmed */
    }
    /* --- kill debounce: a brief glitch never confirms, and re-arms the timer --- */
    { killdb_t k = {0};
      CHECK(!kill_confirmed_m(&k, true, 0));
      CHECK(!kill_confirmed_m(&k, false, 10));                  /* glitch cleared @10ms */
      CHECK(!kill_confirmed_m(&k, true, 20));                   /* new activation, timer resets */
      CHECK(!kill_confirmed_m(&k, true, 20 + KILL_DEBOUNCE_MS - 1));
      CHECK(kill_confirmed_m(&k, true, 20 + KILL_DEBOUNCE_MS)); /* confirms from the NEW start */
    }

    /* --- RPM from tach interval (one spark/rev, 2-stroke single) --- */
    CHECK(rpm_from_interval_ms(30) == 2000);
    CHECK(rpm_from_interval_ms(40) == 1500);
    CHECK(rpm_from_interval_ms(10) == 6000);
    CHECK(rpm_from_interval_ms(0)  == 0);       /* guard against div-by-zero */
    CHECK(rpm_from_interval_ms(60000) == 1);

    /* --- engine caught: RPM holds above threshold for the stable window --- */
    { ecu_t e = { .state = S_START, .rpm = 0, .crank_start = 0 };
      ecu_tick(&e, 100);  CHECK(e.state == S_START);            /* cranking, no catch */
      e.rpm = RPM_CAUGHT_THRESHOLD + 300;                        /* fires, spins up */
      ecu_tick(&e, 1000); CHECK(e.state == S_START);            /* hold just started */
      ecu_tick(&e, 1000 + RPM_CAUGHT_STABLE_MS - 1); CHECK(e.state == S_START);
      ecu_tick(&e, 1000 + RPM_CAUGHT_STABLE_MS);
      CHECK(e.state == S_RUN && !e.starter_on);                  /* caught -> RUNNING, starter off */
    }
    /* --- crank timeout: never catches -> stop, back to IDLE_SAFE --- */
    { ecu_t e = { .state = S_START, .rpm = 500 /* cranking, below threshold */, .crank_start = 0 };
      ecu_tick(&e, CRANK_TIMEOUT_MS - 1); CHECK(e.state == S_START);
      ecu_tick(&e, CRANK_TIMEOUT_MS);
      CHECK(e.state == S_IDLE && !e.starter_on);
    }
    /* --- a single noisy pulse must NOT fake a catch --- */
    { ecu_t e = { .state = S_START, .rpm = 0, .crank_start = 0 };
      e.rpm = RPM_CAUGHT_THRESHOLD + 500;
      ecu_tick(&e, 500);                          /* caught_since = 500 */
      e.rpm = 0;                                  /* pulse was noise; RPM collapses */
      ecu_tick(&e, 500 + RPM_CAUGHT_STABLE_MS + 50);
      CHECK(e.state == S_START);                  /* hold reset, still cranking */
      CHECK(e.caught_since == 0);
    }
    /* --- loss of signal during a start aborts the crank --- */
    { ecu_t e = { .state = S_START, .rpm = 0, .crank_start = 0, .starter_on = true, .recovering = true };
      ecu_tick(&e, 200);
      CHECK(e.state == S_IDLE && !e.starter_on);
    }
    /* --- tick is a no-op when not STARTING --- */
    { ecu_t e = { .state = S_RUN, .rpm = 0, .crank_start = 0 };
      ecu_tick(&e, CRANK_TIMEOUT_MS + 5000); CHECK(e.state == S_RUN);
    }

    if (g_fail == 0) printf("ALL TESTS PASSED\n");
    else printf("%d CHECK(S) FAILED\n", g_fail);
    return g_fail ? 1 : 0;
}
