# Bench rig behavior — receiver

## Why `#include "receiver_firmware.c"`

Every interesting function in `src/receiver/receiver_firmware.c`
(`on_packet_received`, `handle_valid_packet`, `watchdog_tick`,
`crank_tick`, `step_toward_target`) and every state global (`g_state`,
`g_target_throttle`, `g_current_servo_throttle`,
`g_starter_cooldown_until_ms`, ...) is declared `static` — internal linkage,
file-scope only. `bench_app.c` (`DEVELOPMENT/receiver/firmware/Src/`)
textually pulls the whole file in with `#include "receiver_firmware.c"`,
putting all of that into the same translation unit. That means the bench
rig calls the *actual* safety state machine, not a reimplementation — any
bug or fix in the real file shows up here automatically, and there is zero
risk of the bench copy drifting from production.

The **one** exception is `millis()`. It shipped as a stub returning a
hardcoded `0`, which makes every duration comparison in the file
(`WATCHDOG_RAMP_START_MS`, `MAX_CRANK_MS`, `KILL_DEBOUNCE_MS`-equivalent
bookkeeping, `LINK_RESTORE_STABLE_MS`) permanently false — meaningless
against a clock that never advances. `receiver_firmware.c` was changed by
exactly one line to call the real `HAL_GetTick()` when built for the
target (gated on `USE_HAL_DRIVER`, the macro CubeMX defines project-wide, so
the host-side `gcc -c` compile-check in the top-level `CLAUDE.md` still
returns the placeholder `0` and keeps passing unchanged). This is a
hardware-wrapper fill-in, not a change to command handling, ordering, or any
safety invariant — reviewed by the `safety-reviewer` subagent when it was
made. It's the only change to `src/receiver/` or `src/common/` anywhere in
this bench rig.

Everything else — button/pot reading, debounce, the cruise mirror, servo
PWM, display, buzzer, LEDs — is new, bench-only code in `bench_app.c`.

## Input behavior

| Input | Behavior |
|---|---|
| **Kill button** | Debounced over `KILL_DEBOUNCE_MS` (own dedicated window, mirroring the handle's `kill_confirmed()`). Debounced level sets `CMD_FLAG_KILL` in every packet while held/latched-conceptually — but **latching itself happens in the receiver** (`g_state -> STATE_KILLED`), not in the bench code. Once latched: sticky, red LED on, buzzer beeps once on entry, display blinks 0. **Re-arm = power-cycle or press the Nucleo's reset button** — no button or packet field clears it, matching production. |
| **Start button** | Debounced (`INPUT_DEBOUNCE_MS`) then gated by a `START_HOLD_REQUIRED_MS` hold timer mirroring `start_request_confirmed()`. Once hold-confirmed, `CMD_FLAG_START_REQ` is asserted — the receiver's own independent gate (`IDLE_SAFE` state, throttle ≤ `IDLE_THRESHOLD_FOR_START`, not recovering from loss) still applies on top. While cranking: yellow LED on, servo held at whatever `g_target_throttle` says (idle, typically). Crank ends on release (voluntary) or a forced stop (`MAX_CRANK_MS` backstop or `CRANK_LOSS_ABORT_MS` loss-of-signal abort, both inside the real `crank_tick()`). |
| **Cruise button** | Debounced (`INPUT_DEBOUNCE_MS`), then a small local mirror of the handle's `apply_cruise()` (rising edge toggles, freezes the pot reading as setpoint, disengages on kill or pulling the pot more than `CRUISE_DISENGAGE_THROTTLE_DELTA` above the setpoint). This runs in `bench_app.c`, not the receiver — matches how cruise is resolved on the real handle and is transparent to the receiver. |
| **Pot (throttle)** | Scaled 0–4095 (12-bit ADC) → 0–255, fed either live or frozen (if cruise engaged) into the packet's `throttle` field. |

Every ingestion tick builds a real `throttle_packet_t` (incrementing `seq`,
a real `crc8_compute()` over the first 4 bytes) and calls
`on_packet_received()` — the genuine sync/CRC/sequence validation path, not
a shortcut.

## Output behavior

| Output | Behavior |
|---|---|
| **Servo (PA5/TIM2_CH1)** | Reads `g_current_servo_throttle` (post rate-limit/ramp) every tick, maps 0–255 → 1000–2000µs pulse. |
| **7-segment** | `g_current_servo_throttle * 10 / 256` → a coarse 0–9 readout. This is deliberately coarse (one digit can't show 0–255 or even 0–100) — good enough to confirm direction/rough level on the bench, not a precision readout. Blinks (all segments off ~half the time) while `STATE_KILLED`. |
| **Buzzer** | Short beep (`BENCH_BEEP_MS`) on entering `STATE_KILLED` or `STATE_STARTING`. No tone shaping — passive piezo on this rig just clicks. |
| **Red LED** | On for the entire `STATE_KILLED` duration. |
| **Yellow LED** | On for the entire `STATE_STARTING` (cranking) duration. |
| **Green LED — "engine running" proxy** | Lights only when `STATE_STARTING` transitions back to `STATE_IDLE_SAFE` **voluntarily** (start button released, not a forced stop). Off immediately on kill or on re-entering `STATE_STARTING`. **This is not real engine-caught sensing** — the project has no tach (`docs/OPEN-ITEMS.md`, ADR 0007); it's a proxy for "the pilot released start," which is the closest thing to that signal this system has. Never mistake this for actual engine-running detection in later work. |
| **Heartbeat LED** | Toggles at a fixed ~2.5Hz visible rate (not every raw ingestion tick, which would be too fast to see) — confirms the bench loop hasn't hung. |

### How the green LED tells voluntary from forced

Both a voluntary release and a forced stop (backstop/loss-of-signal abort)
produce the exact same `STATE_STARTING -> STATE_IDLE_SAFE` transition in
`g_state` — `end_crank(bool forced)` only differs internally by also setting
`g_starter_cooldown_until_ms` on a forced stop. `bench_app.c` watches that
same static global (visible via the `#include`) and only lights green if it
did *not* just get set to a future value on this transition. See the
comment in `bench_actuation_tick()` in `bench_app.c` for the exact check.

## Verification checklist (do this on real hardware after flashing)

- [ ] Turning the pot moves the servo smoothly, rate-limited (not snapping).
- [ ] Holding start < `START_HOLD_REQUIRED_MS` and releasing does nothing.
- [ ] Holding start ≥ that duration (with pot near idle) enters
      `STATE_STARTING`: yellow on, then releasing → green on, yellow off.
- [ ] Holding start past `MAX_CRANK_MS` auto-stops the crank: yellow off,
      **green stays off** (forced stop).
- [ ] Pressing cruise while throttle is above `IDLE_THRESHOLD_FOR_START`
      freezes the servo at that setpoint even after releasing the pot to
      idle; pushing the pot well past the setpoint disengages it; pressing
      cruise again disengages it too.
- [ ] Pressing kill: red on, buzzer beeps, servo goes to idle, display
      blinks. Releasing kill and sending more packets does **not** clear it.
      Only a reset/power-cycle re-arms.
- [ ] Kill during a crank or during cruise immediately overrides both.
- [ ] Heartbeat LED blinks continuously and doesn't stall under any of the
      above.
- [ ] `gcc -c -Wall -Wextra -Isrc/common src/receiver/receiver_firmware.c -o receiver.o`
      still succeeds with only the pre-existing expected warning
      (`on_packet_received` unused) — confirms the one-line `millis()`
      change didn't disturb the host-side compile-check.
