# Wiring — receiver bench rig

Board: **STM32L432KC Nucleo-32**. Pin names below are STM32 port/pin names
(e.g. `PA0`) — find the matching physical header pin using ST's Nucleo-32
pinout reference (silkscreen + `UM2179`) since the two Nucleo-32 header rows
don't run in port order. **PA13/PA14 are reserved for the on-board ST-Link
(SWD)** — don't wire anything to them.

All grounds (Nucleo GND, breadboard rails, buzzer, display common, button
returns) must be tied together on one common ground rail — no isolated
grounds on this bench build.

## Pin table

| Function | Pin | Direction | Notes |
|---|---|---|---|
| Servo PWM | PA0 (TIM2_CH1) | out | 50 Hz PWM |
| Throttle pot wiper | PA1 (ADC1_IN2) | analog in | |
| Start button | PA2 | in, internal pull-up | |
| Cruise button | PA3 | in, internal pull-up | |
| Kill button | PA4 | in, internal pull-up | |
| 7-seg segment a | PA5 | out | |
| 7-seg segment b | PA6 | out | |
| 7-seg segment c | PA7 | out | |
| 7-seg segment d | PA8 | out | |
| 7-seg segment e | PA9 | out | |
| 7-seg segment f | PA10 | out | |
| 7-seg segment g | PA11 | out | |
| Piezo buzzer | PB0 | out | |
| Green LED ("engine running" proxy) | PB1 | out | |
| Red LED (killed) | PB2 | out | |
| Yellow LED (cranking) | PB3 | out | |
| Heartbeat LED | PB4 | out | |

## Buttons (start / cruise / kill)

Each button: one leg to the GPIO pin, other leg to GND. Enable the STM32's
**internal pull-up** on each pin in CubeMX (see `cubemx-config.md`) — no
external pull resistor needed. Pressed = pin reads LOW (active-low); this
matches how `handle_firmware.c`'s cruise/start/aux inputs are described
(closed = active) even though on the real handle these come from switches on
different nets — here they're just breadboard pushbuttons standing in for
that same logical signal. The firmware debounces all three in software (see
`bench-behavior.md`) — bounce doesn't need to be fixed in hardware.

## Potentiometer (B103, 10kΩ linear)

3-terminal wiring: outer terminals to 3V3 and GND (either way — it just
flips which end of rotation reads 0 vs 255), wiper (center terminal) to
PA1. This gives a clean 0–3.3V sweep into ADC1_IN2.

## SG90 servo

3 wires: signal → PA0, power → **5V**, ground → GND. The SG90 draws up to
~250mA stalled — **don't power it from the Nucleo's 3V3 pin** (it's not a
servo-grade rail and it's only 3.3V, out of spec for the servo anyway). Use
the Nucleo-32's `5V` header pin (sourced from USB when connected via
ST-Link) for this single low-duty SG90 on the bench. If you see the board
reset or brown out when the servo moves under load, that 5V rail is
marginal — switch to a separate 5V bench supply/USB power brick for the
servo, with grounds still tied together.

## Piezo buzzer

Two-lead piezo: one lead to PB0, other to GND. If it's a passive piezo
element (no built-in oscillator), a plain GPIO high/low won't produce a
tone — the current MVP behavior is on/off only (a click/pop, not a tone),
which is enough for the state-transition beeps this rig uses. Swap to a
`TIM` PWM pin later if you want an audible tone instead of a click.

## 5611AH single-digit 7-segment display

**Do a polarity test before wiring current-limiting resistors in** —
common-anode vs common-cathode isn't confirmed for this part:

1. Multimeter in diode-test mode (or a battery + one loose resistor as a
   quick continuity/light test).
2. Touch probes across the **common pin** (usually the middle pin on one
   side, pin 3 or 8 on most 5611-family 10-pin single-digit packages — check
   which two pins are tied together internally, that's the common) and one
   segment pin.
3. If the segment lights with the red (+) probe on the common pin → **common
   cathode** (common ties to GND, segments driven HIGH through resistors).
   If it lights with the black (−) probe on the common → **common anode**
   (common ties to 3V3, segments driven LOW/sunk through resistors).

Wire the common pin to 3V3 (anode) or GND (cathode) accordingly, and put one
resistor (~220–330Ω — check the datasheet's per-segment forward current,
typically ~20mA max) in series with each of the 7 segment pins (a–g) to
PA5–PA11. The decimal point (dp) segment is intentionally left unconnected —
not needed for a 0–9 readout.

Once you know which polarity you have, set `DISPLAY_COMMON_ANODE` in
`firmware/Core/Inc/bench_app.h` to `1` (anode) or `0` (cathode) — the
firmware inverts its segment-drive logic based on that one `#define`.

## LEDs (green / red / yellow / heartbeat)

Standard LED wiring: GPIO pin → resistor (~220–330Ω, depends on the LED's
forward voltage/current — err toward 330Ω if unsure) → LED anode → LED
cathode → GND.
