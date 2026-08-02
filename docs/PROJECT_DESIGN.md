# Paramotor Drive-by-Wire Throttle — Project Design Doc

## Overview
Drive-by-wire paramotor throttle. Handle-mounted STM32 reads trigger position + kill switch + start button, transmits over 2.4GHz (nRF24L01+) to a remote-mounted STM32 near the engine, which drives a servo that pulls the physical throttle cable.

## Architecture

**Handle unit**
- STM32 (Nucleo-32 STM32L432KC or STM32G031 for prototyping)
- Reads trigger position via ADC (12-bit), smoothed with a simple exponential moving average filter
- Reads wireless kill switch and start button (start requires hold, not a single tap)
- Transmits fixed-size packet at 80Hz over nRF24L01+PA+LNA

**Remote unit**
- STM32 (same board family as handle for prototyping)
- Receives packets, validates, runs state machine
- Drives a **remote-mounted servo** (rate-limited) that actuates the engine's throttle **through a cable** (see below)
- Independent watchdog timer for loss-of-signal handling

**Throttle actuation — remote-mounted servo + cable (important)**
- The servo is **NOT mounted at the engine's throttle/carburetor**. It is mounted somewhere convenient on the frame/cage and connected to the engine throttle arm by a **push-pull / Bowden cable**. This is a deliberate design choice:
  - **Space:** the area right at the carb throttle is cramped; remote mounting gives placement freedom.
  - **Vibration:** keeping the servo off the engine spares it the worst of the 2-stroke vibration (fewer failures, better servo life).
  - **Serviceability / RF:** the receiver + servo can sit where wiring, antenna placement, and cooling are easier.
- **Fail-safe direction:** the carburetor's own **return spring pulls the throttle to idle**; the servo pulls *against* it to open. So a depowered servo, a servo failure, or a detached/broken actuation cable lets the spring return the engine to **idle** — the mechanical counterpart of the loss-of-signal watchdog. Confirm the throttle has a positive return-to-idle spring and that the linkage can never jam open.
- **Consequences to design for:** the Bowden cable adds **friction/stiction** (measure pull force through the *full installed cable run*, not the bare throttle cable), routing must avoid tight bends, and the run must tolerate **engine movement** on its rubber mounts (the engine shifts relative to the frame). Full servo travel must still map to the full throttle stroke after any cable slack/stretch.

**Mechanical backup kill switch**
- Completely independent of MCU/radio — wired directly into the ignition kill line (grounds CDI/kill wire on most 2-stroke setups)
- Must function with zero power to any electronics
- Physically distinct in feel/location from trigger and start button, mounted for by-touch operation
- Use locking connectors (JST-SM minimum, Deutsch/Amphenol preferred) given vibration exposure

## Packet Structure (handle → remote, 5 bytes)
| Field | Size | Notes |
|---|---|---|
| Sync byte | 1 | Fixed value 0xA5, marks valid packet start |
| Sequence number | 1 | Rolls 0–255, detects stale/dropped/duplicate packets |
| Throttle position | 1 | 0–255 mapped from handle ADC |
| Command flags | 1 | Bitfield: bit0=kill, bit1=start-request |
| CRC8 | 1 | CRC-8/MAXIM over first 4 bytes, independent of radio-layer CRC |

Command flags are bits, not an enum values, so kill and start states are never ambiguous and kill can always be checked first regardless of what else is in the packet.

## Receiver State Machine
States: `IDLE_SAFE`, `STARTING`, `KILLED` (no `RUNNING` — manual crank, no
tach, see ADR 0007; `IDLE_SAFE` covers both "engine off" and "engine
running", the pilot is the one who knows which)

Per-packet validation order (discard entirely if any step fails):
1. Sync byte check
2. CRC8 check
3. Sequence number check (must be newer, accounting for 0–255 rollover)

Per-packet command handling order (kill always checked first):
1. **Kill** — if set, cut ignition immediately, → `KILLED`, ignore rest of packet
2. **Killed state is sticky** — only a physical/mechanical re-arm can clear it, no wireless command can
3. **Start** — only actionable from `IDLE_SAFE`, only if packet throttle ≤ idle threshold, only if link isn't in post-loss recovery window
4. **Throttle** — applied only if not in post-loss recovery window, rate-limited per tick

## Watchdog / Loss-of-Signal Logic
Runs independently of packet reception, on its own timer:
- **Threshold A** (~175ms since last valid packet): begin linear ramp of throttle down to idle over a fixed duration (~400ms) — not instant, to avoid a sudden idle mid-flight being its own hazard
- **Threshold B** (~600ms): fully committed to idle, hold, mark link as "recovering"
- **Link recovery**: once packets resume, link must be continuously valid for a stability window (~300ms) before throttle is allowed to respond to pilot input again — prevents a flickering link from causing throttle hunting

All thresholds and ramp durations are **fixed compile-time constants**, tuned during bench testing, not adjustable in flight.

## Tunable Constants Reference

Every constant below is a compile-time `#define` — there is no runtime
tuning by design (see Design Decisions Log). All of them live in
`src/common/`, so they're shared by both `handle` and `receiver` builds:
change one, rebuild both firmwares, rerun `test/test_logic.c`, and
re-verify on the bench (or the `DEVELOPMENT/receiver` rig) before trusting
it in the air.

### Protocol timing & throttle shaping (`throttle_protocol.h`)

| Constant | Default | Controls | Tuning notes |
|---|---|---|---|
| `HANDLE_TX_RATE_HZ` | 80 | Packets/sec the handle transmits | `HANDLE_TX_PERIOD_MS` is derived from this — don't set it directly. Higher gives fresher throttle data and a faster-reacting watchdog, at the cost of radio airtime/power. |
| `WATCHDOG_RAMP_START_MS` | 175 | Time since the last valid packet before the receiver starts ramping throttle to idle | Must stay comfortably above the normal inter-packet gap (~12.5 ms at 80 Hz) or normal jitter will trigger false ramps. Lower = faster fail-safe reaction. |
| `WATCHDOG_FULL_IDLE_MS` | 600 | Time since the last valid packet before the receiver commits fully to idle and marks the link "recovering" | Must be greater than `WATCHDOG_RAMP_START_MS`. Too low risks committing to idle during a brief, recoverable glitch. |
| `RAMP_TO_IDLE_DURATION_MS` | 400 | Duration of the linear ramp from current throttle down to idle, once triggered | Shorter = snappier fail-safe but a more abrupt throttle cut; longer = gentler but leaves partial throttle applied longer during a real loss event. |
| `LINK_RESTORE_STABLE_MS` | 300 | After a loss event, how long the link must stay continuously valid before pilot throttle input is honored again | Higher = more confidence the link is genuinely back; lower = faster recovery but more exposure to a flickering link causing throttle hunting. |
| `MAX_THROTTLE_STEP_PER_TICK` | 6 (0–255 units) | Max throttle change per control tick, always — not just during recovery | Higher = snappier response; lower = smoother servo motion and less mechanical shock on the cable run. |
| `THROTTLE_DEADBAND` | 3 (0–255 units) | Handle-side hysteresis: the mapped trigger value must move at least this far before a new value is transmitted (0 and 255 always update) | Higher = less servo "hunting" from hand/engine vibration, at the cost of coarser resolution. |
| `IDLE_THROTTLE_VALUE` | 0 | What "idle" means on the 0–255 scale, matching your servo/cable mapping | The watchdog ramp target and the start gate below are both anchored to this — set it to your actual idle cable position. |
| `IDLE_THRESHOLD_FOR_START` | 15 (~6% of 0–255) | Throttle must be at/below this for a start request to be honored | Only checked at the `IDLE_SAFE → STARTING` transition itself — not re-checked once cranking, so the pot/trigger is free to move once a crank has begun. Higher tolerates a not-quite-zero idle reading; lower demands a stricter "really closed" throttle before allowing a crank. |
| `START_HOLD_REQUIRED_MS` | 600 | How long the start button must be held before a start request is sent | Higher reduces the chance of an accidental brush-start; lower is quicker to initiate cranking. |
| `KILL_DEBOUNCE_MS` | 30 | How long the kill line must read "kill" continuously before it latches | Kill is wired fail-safe, so this never defeats a genuine press or a severed wire — only delays recognizing it slightly. Higher rejects more vibration/contact-bounce glitches; lower reacts faster to a real kill. |
| `INPUT_DEBOUNCE_MS` | 20 | Debounce window for cruise + accessory inputs | Higher rejects more bounce; lower is more responsive but risks a single press registering as multiple toggles. |
| `CRUISE_DISENGAGE_THROTTLE_DELTA` | 10 (0–255 units) | How far the trigger must move above the frozen cruise setpoint before cruise disengages | Higher tolerates a light bump on the trigger while cruising; lower disengages cruise more readily on any throttle movement. |

### Manual crank bounds (`throttle_protocol.h`, receiver-only; see ADR 0007)

| Constant | Default | Controls | Tuning notes |
|---|---|---|---|
| `MAX_CRANK_MS` | 2000 | Hard backstop: a single crank energization can never exceed this, even if the button is held or `START_REQ` sticks asserted | Needs bench tuning against the actual Moster 185 cold-start behavior (see `docs/OPEN-ITEMS.md`) — long enough to reliably catch, short enough to protect the starter motor. |
| `CRANK_LOSS_ABORT_MS` | 175 | Stop cranking if no valid packet arrives for this long | Deliberately faster than the general watchdog thresholds above, since cranking blind on a dead link is worse than just holding throttle. Too low risks aborting a normal crank on a brief hiccup. |
| `STARTER_COOLDOWN_MS` | 3000 | After a *forced* crank stop (backstop or loss-abort — not a normal voluntary release) refuse a new crank this long | Higher gives more starter-motor protection between forced stops; lower allows a quicker retry once the fault clears. Never applies after a normal release. |

### Battery monitor (`battery_monitor.h`, shared by both ends)

| Constant | Default | Controls |
|---|---|---|
| `BATTERY_POLL_MS` | 500 | How often the pack voltage is re-read and the LED bar refreshed |
| `BATTERY_BUZZ_ON_MS` | 120 | Buzzer on-time within each low-battery beep |
| `BATTERY_BUZZ_PERIOD_MS` | 2000 | Time between low-battery beeps |

Not a `#define`, but worth knowing about: the actual pack voltage curve
(`full_mv`/`empty_mv`/`low_mv`/`led_count`) is set per unit — `HANDLE_BATT`
in `handle_firmware.c` and `RX_BATT` in `receiver_firmware.c`. Both are
currently placeholder values and **must be measured against your real packs**
before the LED bar or low-battery buzzer mean anything.

### Not tuning knobs

`PACKET_SYNC_BYTE`, the `CMD_FLAG_*` bit assignments, `PACKET_SIZE`, and
`PACKET_CRC_LEN` are protocol-format constants, not performance knobs —
both `handle` and `receiver` must agree on them exactly (see
`protocol-guardian`); there's no "desired performance" tradeoff to tune.

## Design Decisions Log
- **No radio-level ack** (`setAutoAck(false)`) — fixed-rate send + sequence number + watchdog covers command delivery without ack round-trip latency. Open question: revisit for kill specifically if confirmed-delivery matters more there.
- **CRC8/MAXIM chosen** over rolling custom checksum — small, well-tested, easy to hand-verify against known test vectors.
- **Servo spec** — pull force/travel must be measured across the full stroke (fish scale, not just one point) **through the full installed cable run** (remote mount adds Bowden friction, so measuring the bare throttle cable understates it) before ordering. Ballpark 15–25kg·cm digital metal-gear servo, continuous-duty rated.
- **nRF24L01+PA+LNA** (not bare module) — range margin needed given outdoor use, body/frame in the RF path. Buy from reputable supplier (Mouser/DigiKey), not cheap clones, given safety-critical context.

## Open Items / Not Yet Decided
- ~~What signal indicates "engine caught" to transition `STARTING` → `RUNNING`?~~ RESOLVED (ADR 0007): start is **manual** — the pilot is the catch sensor, so there is no `STARTING` → `RUNNING` transition and no tach in the controller.
- Actual measured distance/RF environment between handle and remote unit (through frame/cage/body) — determines whether PA+LNA gives enough margin
- Final servo selection pending cable force measurement
- Whether kill command should get an ack/confirmed-delivery path

## Toolchain
- STM32CubeIDE (includes CubeMX for peripheral config)
- ST-Link drivers (built into Nucleo boards)
- RF24 by TMRh20 (needs STM32 HAL port — original is Arduino-first)
- Git for version control
- Serial terminal (PuTTY or CubeIDE console) for debug prints
- Logic analyzer (cheap Saleae-compatible) for packet timing / PWM / kill relay verification
- Oscilloscope for servo PWM and kill line signal integrity checks
