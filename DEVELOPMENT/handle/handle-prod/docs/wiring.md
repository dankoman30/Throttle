# Wiring — handle-prod (production handle/transmitter, not a bring-up board)

**Visual reference:** `wiring-diagram.html` (open in a browser) draws every
pin/component below as a schematic; `trigger-filter-wiring.html` is a
focused close-up of just the trigger's RC anti-alias filter sub-circuit.
Both are generated from this doc and should be kept in sync with it, not
treated as an independent source of truth.

Board: **STM32L432KC Nucleo-32** — a fresh, unmodified board (SB16/SB18
solder bridges NOT removed), same reasoning as receiver-prod: the pin plan
below never uses both sides of either bridge pair (`PA5`↔`PB7`, `PA6`↔`PB6`)
at once, so no desoldering is needed.

Same Arduino-label convention as the other wiring docs: every pin below is
given as **Arduino label (STM32 name)**.

## Pin table

| Function | Arduino label | STM32 pin | Direction | Resistor | Notes |
|---|---|---|---|---|---|
| AUX1 indicator LED | A0 | PA0 (`AUX1_LED`) | out | 220–330Ω | mirrors this board's own AUX1 latch state |
| Radio CE | A1 | PA1 (`NRF_CE`) | out | - | |
| Radio CSN | A2 | PA3 (`NRF_CSN`) | out | - | |
| Kill switch | A3 | PA4 (`KILL_SW`) | in, pull-up | - (internal) | active-high (open/broken = kill) - currently a NO jumper stand-in, see "Kill switch" below |
| Trigger position | A4 | PA5 (`TRIGGER_ADC`, `ADC1_IN10`) | analog in | 100kΩ pull-down (wiper node → GND) | see "Trigger" below |
| AUX2 switch (smoke) | A5 | PA6 (`AUX2_SW`) | in, pull-down | - (internal) | momentary, active-high (closed = on) - other leg to **3V3**, not GND |
| Cruise button | A6 | PA7 (`CRUISE_BTN`) | in, pull-down | - (internal) | active-high (closed = pressed) - other leg to **3V3**, not GND |
| Start button | D3 | PB0 (`START_BTN`) | in, pull-up | - (internal) | active-low (pressed = LOW) |
| AUX1 switch (strobe) | D6 | PB1 (`AUX1_SW`) | in, pull-down | - (internal) | momentary button, but **latched in firmware** (toggle on/off) - other leg to **3V3**, not GND |
| Status LED red | D9 | PA8 (`LED_RED`) | out | 220Ω | |
| Status LED green | D1/TX | PA9 (`LED_GREEN`) | out | 220Ω | |
| Cruise indicator LED | D0/RX | PA10 (`CRUISE_LED`, relabeled from `LED_BLUE` 2026-08-09) | out | 220–330Ω | Same pin, now drives a standalone single-color blue LED instead of the tri-color package's blue lead — see "Status LED" below. |
| AUX2 indicator LED | D10 | PA11 (`AUX2_LED`) | out | 220–330Ω | mirrors this board's own live AUX2 level |
| Radio SCK | D13 | PB3 (`SPI1_SCK`) | out | - | onboard LD3 flickers with SPI clock - harmless |
| Radio MISO | D12 | PB4 (`SPI1_MISO`) | in | - | |
| Radio MOSI | D11 | PB5 (`SPI1_MOSI`) | out | - | |

Cruise, AUX1, and AUX2 use the opposite pull convention from kill/start
(pull-down vs pull-up), so their "other leg" goes to 3V3 instead of GND -
easy to mix up wiring several buttons in a row (confirmed the hard way on
the bench: wiring cruise/AUX1 to GND like kill/start meant those pins
could never read HIGH, so neither button did anything until corrected).

16 of 17 usable pins committed, 1 spare (`PA2`/`VCP_TX` - wired to the
ST-Link VCP by default; leave it alone unless USB serial debug is ever
needed).

## Radio (nRF24L01+PA+LNA)

Same module/wiring/decoupling as every other board in this project (10-100µF
electrolytic + 0.1µF ceramic at the module's V+/GND). See
`DEVELOPMENT/radio/README.md` for the bring-up history.

## Trigger

**This is the actual throttle trigger position sensor** - not a placeholder,
unlike the bench rig's simulated pot. `read_throttle_position()` in
`src/handle/handle_firmware.c` calls out the fail-safe wiring requirement
explicitly:

- Pot's low (idle) end → **GND**
- Pot's high end → **3V3**
- Wiper → **A4 (PA5)**

Orient the pot so the wiper idles toward the GND end at trigger release -
**a broken/disconnected wiper wire must read ~0 (idle), never a spurious
high throttle.** A 100kΩ pull-down is wired directly on the ADC line as
extra insurance against a wiper-wire-only disconnect (in addition to, not
instead of, the pot's own resistive path) - same fail-safe-state philosophy
as the kill switch's polarity. High enough not to meaningfully load the
pot's live signal, low enough to firmly define ~0V if the wiper wire opens.
**Must be wired in parallel** (a separate leg from the wiper node straight
to GND) - **not** spliced in series into the wiper wire itself, which
would defeat the fail-safe purpose entirely (the pin would still float if
the wire broke anywhere in that chain) and would also starve the ADC's
very short sampling time (`ADC_SAMPLETIME_2CYCLES_5`, tuned for a
low-impedance source) of enough time to charge the sample capacitor.

**Not yet measured**: real ADC min/max at full release / full pull.
`read_throttle_position()`'s 0-4095 → 0-255 mapping currently assumes the
full rail-to-rail range; retune once the pot is wired and its actual travel
is measured on the bench.

**Anti-alias RC filter (2026-08-14, wired; corrected + upgraded to ceramic
2026-08-17):** hand/engine vibration above the 80Hz sample rate's Nyquist
aliases and can't be fixed after the fact - see `docs/OPEN-ITEMS.md`
"Trigger-ADC anti-alias + oversampling" for the full reasoning. **R1 = 1kΩ
in series between the wiper and the node; C1 = 2.2µF ceramic (50V,
EIA-coded `225`) from that node to GND, cutoff ≈72Hz** (deliberately
chosen below the original ~100Hz estimate for more margin under the
Moster 185's ~120-130Hz fundamental vibration frequency at max RPM). C1 is
a ceramic capacitor - **not polarized, no lead orientation to worry
about** (this is also why it replaced the original electrolytic - see
below). Sits at the exact same node as the 100kΩ pull-down above, in
parallel with it - the pull-down is untouched.

**Wiring bug found + fixed (2026-08-17).** From the original 2026-08-14
install until this date, R1 was physically wired on the *wrong* side of
the filter node - i.e. `wiper → [node: C1, 100kΩ pull-down] → R1 → pin`,
instead of the intended `wiper → R1 → [node: C1, 100kΩ pull-down, pin]`.
Two consequences:
- **The calculated 72Hz cutoff was never actually in effect.** With C1 on
  the source side of R1, the filter's real series resistance was the
  pot's own internal wiper resistance (which varies with trigger
  position and is generally well under 1kΩ), not the fixed, known 1kΩ -
  so the real cutoff was higher than 72Hz and drifted with trigger
  position instead of being fixed. Any bench data collected in this
  window (e.g. the 2026-08-15 held-position jitter check) didn't actually
  validate the filter's frequency response - only a static-position
  jitter check, which this wiring bug wouldn't have shown up in either
  way.
- **The wiper-wire fail-safe was never actually compromised** - the
  pull-down was still directly on the node the wiper wire lands on, so a
  broken wiper wire still correctly pulled the pin to 0V through R1 (no
  current flows at DC steady-state, so no drop across R1). But an **R1
  open-failure would have left the pin fully floating**, stranded past
  the pull-down with no fail-safe path - a real gap in a vibration-heavy
  environment where lead/solder fatigue is plausible. The corrected
  topology (pull-down at the same node as the pin, independent of R1)
  protects against both failure modes, not just a broken wiper wire.

The original on-hand ceramics (0.1µF) were too small to reach ~72Hz
without a series R high enough to meaningfully load against the
pull-down - a proper-value ceramic kit arrived 2026-08-17 and was used for
this swap instead.

**Required matching change:** the trigger channel's ADC sampling time in
CubeMX needs lengthening from the current very short
`ADC_SAMPLETIME_2CYCLES_5` - that won't let the sample capacitor charge
through the new series resistance. The firmware oversampling half
(`TRIGGER_OVERSAMPLE_COUNT`) is already in place and doesn't need this to
work, but the two together are what actually solves aliasing.

## Kill switch

Fail-safe: **normally-closed**, pull-up already enabled in CubeMX, so open
or a broken wire reads HIGH = kill (the failure state is the safe state).
One leg to **A3 (PA4)**, other leg to **GND**. See
`docs/decisions/0003-fail-safe-kill-polarity.md`.

**Currently a bench-test stand-in only**: the switch on hand is
normally-open, which is backwards for this design (idle would read as
permanent kill, and a broken wire would fail silently instead of toward
kill). Bench-testing everything else uses a plain jumper wire bridging the
two leads closed. **Must be swapped for a genuinely normally-closed
switch/contact block before this is wired for real** - see
`docs/OPEN-ITEMS.md`.

## Start button

Momentary, pull-up, active-low (pressed = LOW) - same convention as the
receiver's local start button. One leg to **D3 (PB0)**, other leg to
**GND**. Hold-to-start timing (`START_HOLD_REQUIRED_MS`) is handled in
firmware, not here.

## Cruise button

Momentary, pull-down, active-high (closed = pressed). One leg to
**A6 (PA7)**, other leg to **3V3**.

## AUX1 switch (strobe) — latching

**Confirmed working on real hardware 2026-08-09** — press-to-latch-on,
press-again-to-off, receiver-side AUX1 output tracking correctly.

Physically a plain momentary pushbutton, pull-down, active-high (closed =
on) - same convention and same physical switch type as cruise. One leg to
**D6 (PB1)**, other leg to **3V3**. The latching behavior (press once to
turn on, press again to turn off) is entirely in firmware
(`handle_firmware.c`'s `build_packet()`, toggling on a rising edge - same
shape as `apply_cruise()`'s toggle) - the switch itself is not a
maintained-position part.

## AUX2 switch (smoke) — momentary

**Confirmed working on real hardware 2026-08-09** — on only while held,
receiver-side AUX2 output tracking correctly.

Physically identical wiring style to AUX1/cruise (momentary pushbutton,
pull-down, active-high, other leg to 3V3), but **no latching in
firmware** - it's a pure live level, on only while held. One leg to
**A5 (PA6)**, other leg to **3V3**.

## AUX1 / AUX2 indicator LEDs

**Confirmed working on real hardware 2026-08-09.**

Local-only indicators - each shows what THIS board is currently
commanding, not confirmation the receiver received/applied it (no
telemetry downlink exists). Standard single LEDs, same style as
receiver-prod's original AUX test LEDs:

- AUX1: **A0 (PA0)** → 220–330Ω resistor → LED anode → LED cathode → GND.
  Lit while AUX1 is latched on.
- AUX2: **D10 (PA11)** → 220–330Ω resistor → LED anode → LED cathode →
  GND. Lit while the AUX2 button is actively held.

## Status LED (red + green — bi-color, not the original tri-color package)

**2026-08-09: the tri-color package's blue lead was disconnected** - status
now only needs red+green (see "Behavior" below), and blue was freed up for
the separate cruise indicator instead (see "Cruise indicator LED" below).

**Confirm common-cathode and the physical pin order before wiring** - do
not assume this is the identical part/pinout as receiver-prod's LED even if
it's nominally "the same type"; verify with a multimeter/continuity check
the same way we did for that one (common pin → GND, each color pin through
a resistor → 3.3V, confirm which physical lead lights which color).

- Common → **GND**
- Red → 220Ω resistor → **D9 (PA8)**
- Green → 220Ω resistor → **D1/TX (PA9)**
- Blue → **disconnected** - was → 220Ω resistor → `PA10`, now freed for
  the cruise LED below.

**Behavior** (see `DEVELOPMENT/handle/handle-prod/Inc/handle_app.h` for the
authoritative writeup, priority highest first): kill latched → red,
long-long blink; holding start → yellow (red+green together), solid;
running proxy (latched on release of a confirmed start-hold, never a
telemetry-confirmed fact) → green, solid; default/armed-never-started →
red, lub-dub heartbeat. This is what shows right after boot or a re-arm -
not green, unlike before this change.

## Cruise indicator LED

**New 2026-08-09**, standalone single-color blue LED (not part of the
tri-color package above) - same wiring style as the AUX1/AUX2 indicator
LEDs: **D0/RX (PA10)** → 220–330Ω resistor → LED anode → LED cathode →
GND. Solid while `g_cruise_engaged` is set, off otherwise - no logic
change from before, just relocated off the status LED. Independent of the
status LED's state model entirely.

## Known gaps — deliberately left unconnected

- **Handle battery power**: onboard battery *sensing* was dropped entirely
  (2026-08-08 - standalone battery meters on the pack instead; 2026-08-17 -
  no battery sensing anywhere in this project now, see
  `docs/OPEN-ITEMS.md` "Battery readout wiring"), but the board still needs
  an actual power source for field use, separate from bench power over USB.
  Pack chemistry/voltage is still open - see `docs/OPEN-ITEMS.md` "Servo
  power architecture (decision)" (handle's own supply isn't blocked on
  firmware, just on picking a pack).

## Power rails: 3V3 vs 5V vs VIN

Same guidance as the other boards: **3V3** for the LED/logic/pot rail. No
5V need here (no servo on this board). **VIN** unconnected while
bench-powered over USB.
