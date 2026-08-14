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
stays visually verifiable without adding hardware.

## Implementation (2026-08-14)

- `src/common/unit_config.h` — the per-unit config header, **tracked in
  git** with `UNIT_NUMBER` defaulting to `0xFF` (deliberate unpaired
  bench/test build, not the reserved-and-refused `0x00`). Building a real
  unit means editing this locally and not committing the edit back — see
  the file's own header comment.
- `UNIT_NUMBER == 0x00` triggers a **compile-time `#error`** — a forgotten
  edit doesn't just risk shipping the wrong address, it doesn't build at
  all. This only catches "never touched"; it can't distinguish a genuine
  registry number from a left-at-`0xFF` build someone forgot to change
  before flashing real hardware, so `docs/OPEN-ITEMS.md` carries a
  standing, intentionally-never-checked-off reminder for that per-build
  discipline.
- `nrf24_handle_t` (`src/common/nrf24.h`) gained an `addr` field, following
  the same parameterized-not-hardcoded pattern already used for
  `hspi`/`ce_port`/`ce_pin`/`csn_port`/`csn_pin` — `nrf24.c` stays fully
  unit-agnostic, same as it's agnostic about which board it's running on.
  `handle_app.c`/`prod_app.c` each build their 5-byte address from
  `UNIT_NRF24_ADDR_PREFIX` + `UNIT_NUMBER` and set it before calling
  `nrf24_init()`.
- Bonus, not originally planned: test builds (`UNIT_NUMBER == 0xFF`) get a
  hardware-visible tell — both boards' status LED heartbeat pattern
  renders **inverted** (mostly on with brief off blips, instead of mostly
  off with brief pulses) via `UNIT_IS_TEST_BUILD`, so a bench/test unit is
  visually unmistakable from a real paired one without needing a debugger.
