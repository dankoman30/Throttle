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
- Drives servo (rate-limited) which pulls throttle cable
- Independent watchdog timer for loss-of-signal handling

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
States: `IDLE_SAFE`, `RUNNING`, `STARTING`, `KILLED`

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

## Design Decisions Log
- **No radio-level ack** (`setAutoAck(false)`) — fixed-rate send + sequence number + watchdog covers command delivery without ack round-trip latency. Open question: revisit for kill specifically if confirmed-delivery matters more there.
- **CRC8/MAXIM chosen** over rolling custom checksum — small, well-tested, easy to hand-verify against known test vectors.
- **Servo spec** — pull force/travel must be measured directly off the actual throttle cable (fish scale across full stroke, not just one point) before ordering. Ballpark 15–25kg·cm digital metal-gear servo, continuous-duty rated.
- **nRF24L01+PA+LNA** (not bare module) — range margin needed given outdoor use, body/frame in the RF path. Buy from reputable supplier (Mouser/DigiKey), not cheap clones, given safety-critical context.

## Open Items / Not Yet Decided
- What signal indicates "engine caught" to transition `STARTING` → `RUNNING`? (RPM sense, vibration switch, or timed window — needs to be picked before that transition can be implemented)
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
