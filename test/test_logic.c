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

/* --- mirror of receiver_firmware.c manual-crank logic (pure form) ---
 * Crank control is split across handle_valid_packet (rising-edge begin /
 * release end) and crank_tick (max-crank + loss-of-signal backstops); both are
 * mirrored here to test without the HAL stubs. */
typedef enum { S_IDLE, S_START } st_t;
typedef struct {
    st_t     state;
    uint32_t crank_start;
    uint32_t cooldown_until;
    bool     start_prev;
    bool     starter_on;
} crk_t;

/* one received packet: START_REQ present?, throttle at idle?, mid-recovery? */
static void crk_packet(crk_t *c, bool start_req, bool throttle_ok, bool recovering, uint32_t now) {
    if (start_req && !c->start_prev &&
        c->state == S_IDLE && throttle_ok && !recovering &&
        (int32_t)(now - c->cooldown_until) >= 0) {
        c->state = S_START; c->crank_start = now; c->starter_on = true;
    } else if (!start_req && c->state == S_START) {
        c->starter_on = false; c->state = S_IDLE;   /* voluntary release: no cooldown */
    }
    c->start_prev = start_req;
}

/* independent tick: forced stops (loss / max-crank) impose a cooldown */
static void crk_tick(crk_t *c, bool link_lost, uint32_t now) {
    if (c->state != S_START) return;
    if (link_lost || (now - c->crank_start) >= MAX_CRANK_MS) {
        c->starter_on = false; c->cooldown_until = now + STARTER_COOLDOWN_MS; c->state = S_IDLE;
    }
}

/* --- mirror of handle_firmware.c throttle deadband/hysteresis --- */
static uint8_t deadband(uint8_t mapped, uint8_t *last) {
    int diff = (int)mapped - (int)*last; if (diff < 0) diff = -diff;
    if (mapped == 0 || mapped == 255 || diff >= THROTTLE_DEADBAND) *last = mapped;
    return *last;
}

/* --- mirror of handle_firmware.c:debounce_update --- */
typedef struct { bool raw_last; bool stable; uint32_t last_change; } db_t;
static bool db_update(db_t *d, bool raw, uint32_t now) {
    if (raw != d->raw_last) { d->raw_last = raw; d->last_change = now; }
    if (raw != d->stable && (now - d->last_change) >= INPUT_DEBOUNCE_MS) d->stable = raw;
    return d->stable;
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

    /* --- manual crank: rising edge from IDLE at idle throttle begins a crank --- */
    { crk_t c = {0};
      crk_packet(&c, false, true, false, 100); CHECK(c.state == S_IDLE);   /* no request */
      crk_packet(&c, true,  true, false, 110); CHECK(c.state == S_START && c.starter_on); /* rising edge */
      crk_tick(&c, false, 110 + 500);          CHECK(c.state == S_START && c.starter_on); /* held, within cap */
    }
    /* --- won't begin unless throttle is at idle --- */
    { crk_t c = {0};
      crk_packet(&c, true, false /* throttle high */, false, 100);
      CHECK(c.state == S_IDLE && !c.starter_on);
    }
    /* --- an already-held START_REQ does NOT begin a crank; it needs a fresh edge --- */
    { crk_t c = { .start_prev = true };
      crk_packet(&c, true, true, false, 100);
      CHECK(c.state == S_IDLE);
    }
    /* --- voluntary release stops the crank with NO cooldown -> instant re-crank --- */
    { crk_t c = {0};
      crk_packet(&c, true,  true, false, 100); CHECK(c.state == S_START);
      crk_packet(&c, false, true, false, 200); CHECK(c.state == S_IDLE && !c.starter_on);
      CHECK(c.cooldown_until == 0);                        /* no cooldown on a normal release */
      crk_packet(&c, true,  true, false, 210); CHECK(c.state == S_START); /* re-crank immediately */
    }
    /* --- max-crank backstop force-stops a held crank and imposes cooldown --- */
    { crk_t c = {0};
      crk_packet(&c, true, true, false, 0);      CHECK(c.state == S_START);
      crk_tick(&c, false, MAX_CRANK_MS - 1);     CHECK(c.state == S_START);
      crk_tick(&c, false, MAX_CRANK_MS);         CHECK(c.state == S_IDLE && !c.starter_on);
      CHECK(c.cooldown_until == MAX_CRANK_MS + STARTER_COOLDOWN_MS);
      crk_packet(&c, true, true, false, MAX_CRANK_MS + 10); /* still held -> no re-crank (no edge) */
      CHECK(c.state == S_IDLE);
    }
    /* --- loss of signal aborts the crank fast (before the max-crank cap) --- */
    { crk_t c = {0};
      crk_packet(&c, true, true, false, 0);   CHECK(c.state == S_START);
      crk_tick(&c, true /* link lost */, 50); CHECK(c.state == S_IDLE && !c.starter_on);
    }
    /* --- cooldown blocks a fresh crank until it elapses --- */
    { crk_t c = { .cooldown_until = 5000 };
      crk_packet(&c, true,  true, false, 4000); CHECK(c.state == S_IDLE);  /* still cooling down */
      crk_packet(&c, false, true, false, 4500);                           /* release to re-arm the edge */
      crk_packet(&c, true,  true, false, 5000); CHECK(c.state == S_START); /* cooldown elapsed */
    }

    /* --- throttle deadband/hysteresis: small jitter held, larger moves pass --- */
    { uint8_t last = 100;
      CHECK(deadband(100 + THROTTLE_DEADBAND - 1, &last) == 100);   /* within band (above) -> held */
      CHECK(deadband(100 - (THROTTLE_DEADBAND - 1), &last) == 100); /* within band (below) -> held */
      CHECK(deadband(100 + THROTTLE_DEADBAND, &last) == 100 + THROTTLE_DEADBAND); /* moved enough */
    }
    /* --- rails always reachable despite the deadband --- */
    { uint8_t last = 2;
      CHECK(deadband(0, &last) == 0);       /* snaps to full idle even if within band */
      last = 253;
      CHECK(deadband(255, &last) == 255);   /* snaps to full throttle */
    }

    /* --- input debounce: state changes only after INPUT_DEBOUNCE_MS stable --- */
    { db_t d = {0};
      CHECK(!db_update(&d, true, 0));                     /* press seen, not yet stable */
      CHECK(!db_update(&d, true, INPUT_DEBOUNCE_MS - 1));
      CHECK(db_update(&d, true, INPUT_DEBOUNCE_MS));      /* stable -> true */
      CHECK(db_update(&d, false, INPUT_DEBOUNCE_MS + 5)); /* release seen, still true */
      CHECK(!db_update(&d, false, INPUT_DEBOUNCE_MS + 5 + INPUT_DEBOUNCE_MS)); /* stable -> false */
    }
    /* --- input debounce: a bounce that never settles is rejected --- */
    { db_t d = {0};
      db_update(&d, true, 0); db_update(&d, false, 5);
      db_update(&d, true, 8); db_update(&d, false, 12);
      CHECK(!db_update(&d, true, 15));                    /* last edge at 15, not stable long enough */
      CHECK(db_update(&d, true, 15 + INPUT_DEBOUNCE_MS)); /* now held stable -> true */
    }

    if (g_fail == 0) printf("ALL TESTS PASSED\n");
    else printf("%d CHECK(S) FAILED\n", g_fail);
    return g_fail ? 1 : 0;
}
