# 0004 — Cruise control resolved on the handle

**Status:** accepted · **Date:** 2026

## Decision

Cruise control is implemented **entirely on the handle** (`apply_cruise` in
`src/handle/handle_firmware.c`). When engaged, the handle captures the current
trigger position as a setpoint and **transmits that frozen value** in the normal
throttle field; the receiver stays cruise-transparent (`CMD_FLAG_CRUISE` is
informational only).

## Why

- All disengage triggers are physically at the handle — a second cruise-button
  press, kill, or the pilot pulling the trigger above the setpoint — so the
  logic naturally lives there.
- The receiver's safety posture is unchanged: because cruise is just a throttle
  value in the normal field, the **loss-of-signal watchdog still overrides it**.
  If the link drops while "cruising," the receiver ramps to idle exactly as it
  would for any held throttle. Cruise cannot defeat the watchdog.
- Keeps the packet/state machine simple; no new receiver state.

## Key behavioral rule

Cruise disengages only on an **upward** override (trigger pulled above
setpoint + `CRUISE_DISENGAGE_THROTTLE_DELTA`). **Releasing** the trigger below
the setpoint does *not* disengage — that is the entire point of cruise (rest the
hand). To reduce throttle, the pilot presses cruise or kill. This rule was
caught and corrected by `test/test_logic.c` after an initial version wrongly
disengaged on any movement, which would have made cruise unusable.

## Addendum (2026-08-03): a third, delayed escape

Added a third disengage path, still entirely handle-side: if the trigger is
held continuously at/below `CRUISE_REARM_THROTTLE_THRESHOLD` for
`CRUISE_IDLE_REARM_DELAY_MS` (2s) while cruise is engaged, the pilot's *next*
throttle input above that same threshold cancels cruise. This does not
contradict the key behavioral rule above — releasing the trigger still holds
cruise indefinitely by itself, exactly as before. This only fires on a
subsequent deliberate move, giving the pilot a third way to hand control back
(alongside the cruise button and the above-setpoint override): let go, wait a
beat, then just retake the trigger, without needing to pull past the old
setpoint or find the cruise button.

Deliberately uses the **same threshold in both directions** (counts as
"neutral" to start the hold, and as "real input" to cancel afterward) rather
than two separate values: with two different thresholds, a pilot resting
statically in the gap between them could have cruise cancel itself the
instant the hold timer completes, with no new movement at all — a surprising,
unrequested disengage. One shared threshold makes that structurally
impossible: the hold timer can only run while throttle is at/below it, so
becoming armed can never by itself cross above it; only an actual subsequent
move can. Any dip back above the threshold during the wait resets the timer —
it must be one continuous hold, not cumulative.
