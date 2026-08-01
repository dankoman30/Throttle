# DEVELOPMENT/receiver — bench rig (no radio yet)

Runs the **real** receiver safety state machine
(`src/receiver/receiver_firmware.c`, completely unmodified) on an
STM32L432KC (Nucleo-32), fed by breadboard buttons and a potentiometer
instead of the nRF24L01+ radio link. This proves out kill/start/throttle/
watchdog behavior on real hardware before the radio, relays, and production
servo are wired up.

## What this does and doesn't test

**Does:**
- The receiver's packet validation (sync/CRC8/sequence), the kill latch and
  its stickiness, gated start (manual crank model), rate-limited throttle,
  and the independent loss-of-signal watchdog — all running the actual
  functions from `src/receiver/receiver_firmware.c` via
  `#include "receiver_firmware.c"` in `firmware/Core/Src/bench_app.c` (see
  `docs/bench-behavior.md` for why this reuse approach was chosen and
  exactly what runs).
- A hand-built cruise mirror (small, bench-only reimplementation of the
  handle's `apply_cruise()`), since cruise is normally resolved on the
  handle and the receiver treats it as transparent.

**Does not test:**
- The radio link itself (packet loss patterns, RF interference, range).
- The starter solenoid / ignition-cut relays (LED stand-ins only here).
- Real engine-caught detection — the project has no tach (see
  `docs/OPEN-ITEMS.md`, ADR 0007); the green "engine running" LED here is a
  manual-release proxy, not a sensor reading.

## Layout

```
docs/
  wiring.md           pin-by-pin connections, resistor values, 7-seg polarity test
  cubemx-config.md    step-by-step CubeMX peripheral/pin/clock setup
  bench-behavior.md   full behavior spec + verification checklist
schematics/
  receiver-bench.md   breadboard schematic (markdown/ASCII)
firmware/
  Core/Src/bench_app.c   ingestion (buttons/pot) + actuation (servo/display/buzzer/LEDs)
  Core/Inc/bench_app.h
```

`firmware/` is not yet an importable CubeIDE project — only the application
source is committed here. See `docs/cubemx-config.md` for creating the
project (via the CubeIDE wizard, board = NUCLEO-L432KC) directly at this
path and wiring `bench_app.c` in.

## Bill of materials (this bench rig)

- STM32L432KC Nucleo-32
- 3× momentary pushbutton (start, cruise, kill)
- 1× B103 (10kΩ linear) potentiometer
- 1× piezo buzzer
- 1× 5611AH single-digit 7-segment LED display
- 1× SG90 micro servo
- 4× LED (green, red, yellow, + 1 for heartbeat — any color)
- Resistors: 7× for the 7-segment + 4× for the LEDs (~220–330Ω, see `docs/wiring.md`)
