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
