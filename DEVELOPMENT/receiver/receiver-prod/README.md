# DEVELOPMENT/receiver/receiver-prod — production receiver

Runs the **real** receiver safety state machine
(`src/receiver/receiver_firmware.c`, unmodified except three small, reviewed
hardware fill-ins guarded by `RECEIVER_PROD_BOARD` — see `Src/prod_app.c`'s
header comment) on an STM32L432KC (Nucleo-32), driving **real hardware**:
the nRF24L01+ radio link, a servo, a tri-color status LED, and a local
start button — not breadboard simulation. Compare
`DEVELOPMENT/receiver/firmware/` (the bench rig), which proves out the same
state machine fed by local buttons/a pot instead of the radio, and doesn't
drive real actuator hardware.

## Layout

```
docs/
  wiring.md        pin-by-pin connections, resistor values, known wiring gaps
Inc/prod_app.h      LED behavior spec (heartbeat/kill/cranking/battery-low)
Src/prod_app.c      radio ingestion + servo/LED/relay/accessory actuation
receiver-prod.ioc    STM32CubeMX project file
```

Full STM32CubeMX-generated project (HAL/CMSIS drivers under `Drivers/`,
`STM32CubeIDE/.project`/`.cproject`) targeting NUCLEO-L432KC, with
`prod_app.c`/`prod_app.h` wired into `main.c`. Note: this project's
`.project` tracks source files as an explicit linked-resource list (not a
live directory scan) — see the commit that added `prod_app.c` if a new file
in `Src/`/`Inc/` doesn't show up in the IDE after a refresh.

## Board

Reuses "board B" from `DEVELOPMENT/radio/spi-bringup/` — the pin plan
deliberately avoids the `PA5`↔`PB7`/`PA6`↔`PB6` solder-bridge pairs, so this
board's SB16/SB18 were **not** desoldered (unlike the bench rig's board,
which required that mod). See `docs/wiring.md` for the full pin table and
why this works.

## Status

- **2026-08-08: Stage 1 confirmed on real hardware** (standalone, no radio
  link needed) — heartbeat LED blinking, servo settling at idle, and the
  local start button working end-to-end: press → blue LED (cranking),
  release → green LED (engine-running proxy). This is the first real-
  hardware confirmation of the local start button feature added this
  session (see the safety-reviewer-verified fix in
  `src/receiver/receiver_firmware.c`'s git history for the trigger-source
  tracking that closes the loss-of-signal-abort gap).
- **2026-08-08: Stage 2 confirmed on real hardware — full radio→servo path.**
  `spi-bringup`'s TX role now sends a real, protocol-valid
  `throttle_packet_t` (correct sync byte, sequence number, CRC8) with the
  throttle field slowly triangle-sweeping between idle and a moderate
  value. Board B's servo visibly tracked the sweep, live over the air —
  direct confirmation that `on_packet_received()`'s sync→CRC8→sequence
  validation is passing on real hardware for the first time, and that
  received throttle correctly flows through the rate-limited
  `step_toward_target()` path to the servo PWM. This is the first
  confirmed real-hardware run of the complete radio→validate→throttle→
  servo chain.
- **2026-08-08: Stage 3 confirmed on real hardware — wireless KILL and
  START, both correctly gated.** `spi-bringup`'s TX role now also
  auto-tests `CMD_FLAG_START_REQ` (every ~6s, held ~0.9s at idle throttle,
  well under `MAX_CRANK_MS` - always ends via voluntary release) and
  supports a debugger-triggered one-shot `CMD_FLAG_KILL` (not automatic,
  since KILL is sticky on the receiver and firing it on a timer would lock
  board B mid-test). Confirmed: wireless START correctly cycles blue
  (cranking) → green (engine-running proxy, stays lit - visible as an
  amber/yellow tint against the still-blinking red heartbeat) → back to
  normal; wireless KILL correctly latches solid red and stays there
  through a real board-A debug-session interruption, requiring a genuine
  board B power-cycle to re-arm (confirmed the local start button remains
  independently functional after re-arm, and that the TX side's one-shot
  `tx_send_kill` flag correctly self-resets). This is full real-hardware
  confirmation of every command path this board handles - kill, start
  (both wireless and local), and throttle - all currently deferred parts
  (kill relay, starter relay, battery sense) are wiring gaps, not logic
  gaps.
- **Known, deliberate wiring gaps**: kill relay, starter relay, battery
  sense — see `docs/wiring.md` "Known gaps".
