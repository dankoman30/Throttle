# Wiring — receiver bench rig

Board: **STM32L432KC Nucleo-32** (UFQFPN32 package — only 26 usable GPIOs
total). This board's physical header uses **Arduino Nano-style silkscreen
labels** (`A0`–`A7`, `D0`–`D13`, `3V3`, `5V`, `VIN`, `GND`, ...), not the
STM32 port/pin names (`PA0`, `PB3`, ...) used everywhere else in this repo
— the two don't run in any obvious order relative to each other. Every pin
below is given as **Arduino label (STM32 name)** so you can wire directly
from your board's silkscreen without translating.

This mapping is ST's official one (UM2179, "Arduino Nano connectors on
NUCLEO-L432KC" table), cross-checked against the board-lock data below —
`A0`↔`PA0` (locked to MCO), `A7`↔`PA2` (locked to VCP_TX), and `D13`↔`PB3`
(the on-board LED) all line up exactly with what we already knew about
those three pins, which is a solid consistency check on the source.

| Arduino label | STM32 pin | Arduino label | STM32 pin |
|---|---|---|---|
| A0 | PA0 | D0/RX | PA10 |
| A1 | PA1 | D1/TX | PA9 |
| A2 | PA3 | D2 | PA12 |
| A3 | PA4 | D3 | PB0 |
| A4 | PA5 | D4 | PB7 |
| A5 | PA6 | D5 | PB6 |
| A6 | PA7 | D6 | PB1 |
| A7 | PA2 | D7 | PC14 |
| | | D8 | PC15 |
| | | D9 | PA8 |
| | | D10 | PA11 |
| | | D11 | PB5 |
| | | D12 | PB4 |
| | | D13 | PB3 |

Plus `3V3`, `5V`, `VIN`, `GND` (both headers), `RESET` (both headers, tied
to `NRST`), and `AREF`. **SWDIO/SWCLK (PA13/PA14) aren't on this header at
all** — they're only on the separate ST-Link debug section.

**`D0`/`D1` are silkscreened as `D0/RX` and `D1/TX`**, not bare `D0`/`D1` —
they're the primary USART1 pins (PA10=RX, PA9=TX), common enough that ST
prints both labels together. Look for the compound label, right next to
`RST`/`GND` at the top of the left header.

**Reserved/unavailable pins — do not wire anything to these, and don't
reassign them in CubeMX either.** Verified against this board's own
definition file in the installed STM32CubeMX
(`db\plugins\boardmanager\boards\B41_Nucleo_NUCLEO-L432KC_STM32L432KC_Board.ioc`)
and the MCU's pinout XML (`db\mcu\STM32L432K(B-C)Ux.xml`):
- **PA13, PA14** — ST-Link SWD (SWDIO/SWCLK). Not on the Arduino header.
- **PA15** — ST-Link VCP_RX (also JTDI). Not on the Arduino header.
- **A7 (PA2)** — ST-Link VCP_TX (USART2_TX) — the virtual COM port's
  transmit line, wired straight to the ST-Link's USB-serial bridge.
- **A0 (PA0)** — locked by this board to `RCC_CK_IN` (MCO/high-speed clock in).
- **D13 (PB3)** — the on-board green LED, **LD3**.
- **D7, D8 (PC14, PC15)** — 32.768kHz crystal (OSC32_IN/OUT).
- **PB2 does not exist** on this package at all (pin numbering jumps PB1 →
  VDD/VSS → PA8) — an earlier draft of this doc used it by mistake. It has
  no Arduino header label either.

That leaves exactly 17 free GPIOs, which is exactly what this bench rig
needs — no margin, so if you add anything later you'll need to drop
something else or move to a bigger Nucleo.

## Required board mod: cut SB16 and SB18 before wiring A4/A5/D4/D5

**This is not optional for this pin plan — do it before wiring the kill
button, servo, yellow LED, or heartbeat LED.** Confirmed from ST's own
board manual (UM1956, "Table 8. Solder bridges"), this board (Rev C, which
is what ships today) has two solder bridges **closed by default**:

- **SB16**: ties `PB6` (`D5`) to `A5` (`PA6`) for optional I2C SDA support
  on `A5`. Per the manual, when SB16 is on, *"PA6 must be configured as
  input floating"* — i.e. `D5`/`PB6` and `A5`/`PA6` are not independent
  pins, they're electrically the same node.
- **SB18**: same thing for `PA5` (`A4`) and `PB7` (`D4`), I2C SCL.

In this pin plan, `A5`/`PA6` is the **kill button** and `D5`/`PB6` is the
**yellow LED**; `A4`/`PA5` is the **servo PWM** and `D4`/`PB7` is the
**heartbeat LED**. With the bridges closed, each LED's push-pull GPIO
output fights the paired input pin's pull-up/PWM signal through the
bridge — this is exactly what caused the kill button to read permanently
low regardless of wiring, reproduced identically on two separate boards,
until this was traced back to the schematic. It is a documented board
design, not a defect.

**Fix**: locate the solder bridges labeled `SB16` and `SB18` on the PCB
silkscreen (small pads near the `CN4` connector/`PB6`/`PB7` area — look
closely on both sides of the board, they're tiny) and cut them, either by
scoring the connecting trace between the two pads with a hobby knife, or
by removing the solder blob with a soldering iron + solder wick if they're
bridged that way instead. Cutting them is exactly what ST's manual
describes as the "off" state, and restores `A4`/`A5`/`D4`/`D5` to fully
independent operation — precisely what this design already assumes
everywhere else. (They're a reversible-ish modification — a solder blob
can rebridge them later if some future project wants the I2C behavior
instead.)

**Remove the factory jumper cap between `GND` and `D2` before wiring
anything.** The board ships with a spare 2-pin jumper cap parked across
`GND`/`D2` (`PA12`) on CN3 — it's not a functional connection, just a safe
place for ST to park a spare clip (confirmed on ST's community forum; some
of ST's own low-power example projects use that same pin deliberately as a
wake-up input, but that's opt-in for that example, not inherent to the
board). Since `D2`/`PA12` is **7-segment segment f** in this pin plan,
leaving the jumper in place would permanently short that GPIO to ground.
Pull it off and set it aside.

## Power rails: 3V3 vs 5V vs VIN

Use **3V3** for the breadboard's main power rail (the pot) — it's the
board's regulated 3.3V *output*, meant exactly for powering external
components. (The 7-segment display's common pin goes to GND instead — it's
confirmed common-cathode, see below.)

**Don't use VIN for this.** VIN is a power *input* — where you'd connect an
external battery/adapter if powering the board from something other than
USB. Since this rig is powered over USB/ST-Link, VIN is just unconnected;
wiring a rail to it gets you nothing.

This isn't just about convenience: the pot feeds directly into the STM32's
ADC (`A1`/`PA1`, `ADC1_IN6`), and the ADC input range is tied to the chip's
own supply (VDDA ≈ 3.3V). If that rail were somehow at 5V instead, the
wiper could swing above 3.3V and overdrive the ADC pin beyond spec — a real
risk to the MCU, not just a bad reading. 3V3 is the only rail that's safe
to feed into that pin.

The **servo is the one exception** — it gets its own separate connection
from the **5V** pin straight to its power lead (see the SG90 section
below), not the same 3V3 rail as the pot. Don't cross the two: servo power
doesn't go on the 3V3 rail, and the pot/display-common rail doesn't go to
5V.

## Pin table

| Function | Arduino label | STM32 pin | Direction | Notes |
|---|---|---|---|---|
| Throttle pot wiper | A1 | PA1 (ADC1_IN6) | analog in | |
| Start button | A2 | PA3 | in, internal pull-up | |
| Cruise button | A3 | PA4 | in, internal pull-up | |
| Servo PWM | A4 | PA5 (TIM2_CH1) | out | 50 Hz PWM |
| Kill button | A5 | PA6 | in, internal pull-up | |
| 7-seg segment a | A6 | PA7 | out | |
| 7-seg segment b | D9 | PA8 | out | |
| 7-seg segment c | D1/TX | PA9 | out | |
| 7-seg segment d | D0/RX | PA10 | out | |
| 7-seg segment e | D10 | PA11 | out | |
| 7-seg segment f | D2 | PA12 | out | |
| 7-seg segment g | D3 | PB0 | out | |
| Piezo buzzer | D6 | PB1 | out | |
| Green LED ("engine running" proxy) | D12 | PB4 | out | |
| Red LED (killed) | D11 | PB5 | out | |
| Yellow LED (cranking) | D5 | PB6 | out | |
| Heartbeat LED | D4 | PB7 | out | |

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
**A1 (PA1)**. This gives a clean 0–3.3V sweep into ADC1_IN6.

## SG90 servo

3 wires, standard SG90 color code:
- **Brown → GND**
- **Red → 5V**
- **Orange → A4 (PA5)** (signal)

The SG90 draws up to ~250mA stalled — **don't power it from the Nucleo's 3V3 pin** (it's not a
servo-grade rail and it's only 3.3V, out of spec for the servo anyway). Use
the Nucleo-32's `5V` header pin (sourced from USB when connected via
ST-Link) for this single low-duty SG90 on the bench. If you see the board
reset or brown out when the servo moves under load, that 5V rail is
marginal — switch to a separate 5V bench supply/USB power brick for the
servo, with grounds still tied together.

## Piezo buzzer

Two-lead piezo: one lead to **D6 (PB1)**, other to GND. If it's a passive piezo
element (no built-in oscillator), a plain GPIO high/low won't produce a
tone — the current MVP behavior is on/off only (a click/pop, not a tone),
which is enough for the state-transition beeps this rig uses. Swap to a
`TIM` PWM pin later if you want an audible tone instead of a click.

## 5611AH single-digit 7-segment display

**Confirmed common-cathode** per the manufacturer datasheet (XLITX
5611AH, 0.56", ultra-bright red) — no polarity test needed. Wire the
common pin to **GND**. `DISPLAY_COMMON_ANODE` in `firmware/Inc/bench_app.h`
is already set to `0` (cathode) to match.

Datasheet electro-optical specs relevant to the resistor choice:
- Forward voltage (VF): 1.8V typical
- Max continuous forward current per segment: 30mA (absolute max — stay
  well under this)

At the STM32's 3.3V GPIO drive: `R = (3.3V − 1.8V) / I = 1.5V / I`. A
**150–220Ω** resistor gives ~7–10mA per segment — comfortably under both
the LED's 30mA max and a typical GPIO's ~20mA safe drive limit, while
still bright enough for bench use. Put one resistor per segment pin (a–g)
in series, to **A6, D9, D1/TX, D0/RX, D10, D2, D3** (per the pin table
above). The decimal point (dp) segment is intentionally left
unconnected — not needed for a 0–9 readout.

## LEDs (green / red / yellow / heartbeat)

Standard LED wiring: GPIO pin → resistor (~220–330Ω, depends on the LED's
forward voltage/current — err toward 330Ω if unsure) → LED anode → LED
cathode → GND.
