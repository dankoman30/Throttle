# 0009 — Per-unit nRF24 address as a compile-time constant, not a hardware switch

**Status:** accepted · **Date:** 2026

## Decision

Each manufactured handle+receiver pair's unique nRF24 address (see the
"Multi-unit isolation" item in `docs/OPEN-ITEMS.md` for the addressing
scheme itself) is burned into both firmware images as a **compile-time
constant**, replacing `PLACEHOLDER_ADDR` in `src/common/nrf24.c`. It is
**not** read at runtime from a physical DIP switch, rotary switch, or any
other field-adjustable hardware.

## Alternative considered: DIP/rotary switches

The appeal: a single firmware image could be built once and flashed to
every unit, with the address configured afterward per-pair by setting
switches — no per-unit build step, and the address is physically
inspectable without a debugger.

Rejected for three reasons, the first of which is close to disqualifying
on its own:

- **No pins to spare.** `handle-prod` is down to exactly 1 spare GPIO
  (`PA2`/`VCP_TX`, already reserved for the ST-Link VCP) — see
  `DEVELOPMENT/handle/handle-prod/docs/wiring.md`. A binary-encoded switch
  bank needs several GPIOs to read; there is nowhere to put it without
  displacing something already wired. Both ends of a pair need to carry
  the *same* unit number for addressing to work, so the receiver having
  more spare pins doesn't help — the handle is the binding constraint.
- **Vibration is a real failure mode on this vehicle.** The receiver sits
  next to a running 2-stroke engine. A mechanical switch can walk to a
  different position under sustained vibration over time — and unlike
  most vibration-induced failures, this one wouldn't just misbehave, it
  could silently make the receiver stop recognizing its own handle, or
  drift into matching a *different* nearby unit's address — exactly the
  cross-talk scenario this feature exists to prevent. A compile-time
  constant has no physical state to lose.
- **Consistency with an existing project-wide principle.** Every other
  safety-relevant tunable in this codebase (watchdog thresholds, debounce
  windows, kill polarity - see ADR 0003) is a compile-time `#define`, not
  a runtime-configurable value, specifically so nothing can drift or get
  bumped in the field. A switch-configured address would be the one
  exception, without a strong enough reason to justify it here.

## Consequence

Each unit needs its own build (a per-unit config header, not a shared
image + hardware config step) — real manufacturing friction the switch
approach would have avoided. Mitigated by keeping the per-unit value
physically visible another way: a printed label on the unit showing its
assigned number, matching its `docs/UNIT-REGISTRY.md` row, so pairing
stays visually verifiable without adding hardware. The exact per-unit
build mechanism (config header vs. build flag) is implementation work,
tracked in the "Multi-unit isolation" `docs/OPEN-ITEMS.md` item.
