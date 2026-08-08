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
| **Start button** | Debounced (`INPUT_DEBOUNCE_MS`) then gated by a `START_HOLD_REQUIRED_MS` hold timer mirroring `start_request_confirmed()`. Once hold-confirmed, `CMD_FLAG_START_REQ` is asserted — the receiver's own independent gate (`IDLE_SAFE` state, throttle ≤ `IDLE_THRESHOLD_FOR_START` (15/255, ~6%), not recovering from loss) still applies on top, and **only guards entering `STATE_STARTING`** — it is not re-checked once cranking. While cranking: yellow LED on, servo tracks `g_target_throttle` live, same as any other state (see the pot row below) — the pot is not frozen or ignored during a crank, so the pilot can crack the throttle open mid-crank to help a cold engine catch. Crank ends on release (voluntary) or a forced stop (`MAX_CRANK_MS` backstop or `CRANK_LOSS_ABORT_MS` loss-of-signal abort, both inside the real `crank_tick()`). |
| **Cruise button** | Debounced (`INPUT_DEBOUNCE_MS`), then a small local mirror of the handle's `apply_cruise()` (rising edge toggles, freezes the pot reading as setpoint). Disengages on: kill, a second press, pulling the pot more than `CRUISE_DISENGAGE_THROTTLE_DELTA` above the setpoint, or the idle-then-retake escape — pot held continuously at/below `CRUISE_REARM_THROTTLE_THRESHOLD` for `CRUISE_IDLE_REARM_DELAY_MS` (2s), then moved back above that same threshold (see the addendum in `docs/decisions/0004-cruise-on-handle.md` for why one shared threshold, not two). This runs in `bench_app.c`, not the receiver — matches how cruise is resolved on the real handle and is transparent to the receiver. |
| **Pot (throttle)** | Scaled 0–4095 (12-bit ADC) → 0–255, fed either live or frozen (if cruise engaged) into the packet's `throttle` field on *every* ingestion tick. In the receiver, `handle_valid_packet()` applies `pkt->throttle` to `g_target_throttle` unconditionally once past the kill/killed checks (`receiver_firmware.c`, the THROTTLE step) — state (`IDLE_SAFE` or `STARTING`) doesn't gate it, only `g_recovering_from_loss` does. So the pot stays live through a crank; only the *start* button's rising edge additionally requires throttle ≤ `IDLE_THRESHOLD_FOR_START` at that instant. |

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

## Tunable constants — bench rig only

These live in `firmware/Src/bench_app.c` / `firmware/Inc/bench_app.h`, not
`src/common/` — they only affect this bench rig's own display/servo/beep
mapping, not the real safety state machine, so they're safe to change
freely without a `safety-reviewer` pass. For the actual safety-critical
timing/threshold constants (crank timeout, idle threshold for start,
watchdog thresholds, etc. — shared by both `handle` and `receiver`), see
the **Tunable Constants Reference** in `docs/PROJECT_DESIGN.md`.

| Constant | Default | Controls |
|---|---|---|
| `SERVO_PULSE_MIN_US` / `SERVO_PULSE_MAX_US` | 1000 / 2000 | Pulse-width range mapped from throttle 0–255. Adjust to your specific SG90's actual measured travel endpoints if the datasheet range doesn't quite match observed behavior. |
| `DISPLAY_BLINK_MS` | 300 | Blink half-period for the 7-segment display while `STATE_KILLED`. |
| `BENCH_BEEP_MS` | 80 | Buzzer click duration on entering `STATE_KILLED` or `STATE_STARTING`. |
| `HEARTBEAT_PERIOD_MS` | 200 | Heartbeat LED toggle period (~2.5Hz visible rate) — purely a "loop hasn't hung" indicator, not a timing-sensitive value. |
| `DISPLAY_COMMON_ANODE` (in `bench_app.h`) | 0 (cathode) | Segment drive polarity — a hardware-wiring flag, not a performance knob. Must match your actual 7-segment display's datasheet. |

## Verification checklist (do this on real hardware after flashing)

**Confirmed passing on real hardware, 2026-08-01 (start/idle-throttle
interaction added and confirmed 2026-08-02)** — STM32L432KC Nucleo-32,
SB16/SB18 removed, see `wiring.md`:

- [x] Turning the pot moves the servo smoothly, rate-limited (not snapping).
- [x] Holding start < `START_HOLD_REQUIRED_MS` and releasing does nothing.
- [x] Holding start ≥ that duration (with pot near idle) enters
      `STATE_STARTING`: yellow on, then releasing → green on, yellow off.
- [x] Holding start past `MAX_CRANK_MS` auto-stops the crank: yellow off,
      **green stays off** (forced stop).
- [x] Pressing cruise while throttle is above `IDLE_THRESHOLD_FOR_START`
      freezes the servo at that setpoint even after releasing the pot to
      idle; pushing the pot well past the setpoint disengages it; pressing
      cruise again disengages it too.
- [ ] **Idle-then-retake escape (added 2026-08-03, not yet bench-verified):**
      with cruise engaged, turn the pot down to at/below
      `CRUISE_REARM_THROTTLE_THRESHOLD` (~8/255) and hold it there. Confirm
      the servo *stays* at the frozen setpoint (does not drop to idle) for
      the first `CRUISE_IDLE_REARM_DELAY_MS` (2s) — turning the pot up
      *before* the 2s mark should have no effect on cruise. After the 2s
      mark, the very next pot movement above the threshold should disengage
      cruise immediately, with the servo following the pot live from that
      point on. Also confirm: sitting at idle indefinitely past the 2s mark
      *without* moving the pot again does **not** disengage on its own.
- [x] Pressing kill: red on, buzzer beeps, servo goes to idle, display
      blinks. Releasing kill and sending more packets does **not** clear it.
      Only a reset/power-cycle re-arms.
- [x] Kill during a crank or during cruise immediately overrides both.
- [x] Starting requires the pot at/near idle (≤ `IDLE_THRESHOLD_FOR_START`,
      15/255) — pressing start with the pot elevated does nothing, confirmed
      by debugger readout of `g_state`/`pkt->throttle`, not just the servo.
      Turning the pot down to true zero (not just "looks like zero" on a
      breadboard pot) makes start work again.
- [x] Once cranking, the pot remains live: turning it up moves the servo
      immediately, without affecting `g_state` or ending the crank. This is
      intended (`receiver_firmware.c`'s THROTTLE step is unconditional on
      state, only `g_recovering_from_loss` suppresses it) — not bench-specific
      behavior.
- [x] Heartbeat LED blinks continuously and doesn't stall under any of the
      above.
- [x] `gcc -c -Wall -Wextra -Isrc/common src/receiver/receiver_firmware.c -o receiver.o`
      still succeeds with only the pre-existing expected warning
      (`on_packet_received` unused) — confirms the one-line `millis()`
      change didn't disturb the host-side compile-check.
