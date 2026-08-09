# Wiring — receiver-prod (production receiver, not the bench rig)

Board: **STM32L432KC Nucleo-32** — the second `spi-bringup` board ("board B"),
**unmodified** (SB16/SB18 solder bridges NOT removed). This is deliberate:
the pin plan below avoids using both sides of either bridge pair
(`PA5`↔`PB7`, `PA6`↔`PB6`) simultaneously, so no desoldering is needed — see
"Why no solder-bridge rework" below.

This is a **different board and pin plan from the bench rig**
(`DEVELOPMENT/receiver/firmware/`, see `DEVELOPMENT/receiver/docs/wiring.md`)
— it drives real hardware (servo, radio, relays) instead of simulating
wireless input from local breadboard buttons/a pot. Same Arduino-label
convention as that doc: every pin below is given as **Arduino label (STM32
name)**.

## Pin table

| Function | Arduino label | STM32 pin | Direction | Notes |
|---|---|---|---|---|
| Radio CE | A1 | PA1 (`NRF_CE`) | out | |
| Radio CSN | A2 | PA3 (`NRF_CSN`) | out | |
| Kill relay | A3 | PA4 (`KILL_RELAY`) | out | **Not yet wired** — see "Known gaps" |
| Servo PWM | A4 | PA5 (`TIM2_CH1`) | out | 50Hz, see "Servo" below |
| Battery sense | A5 | PA6 (`BATT_SENSE`, `ADC1_IN11`) | analog in | **Not yet wired** — see "Known gaps" |
| Tri-color LED red | A6 | PA7 (`LED_RED`) | out | |
| Local start button | D3 | PB0 (`LOCAL_START_BTN`) | in, internal pull-up | |
| Starter relay | D6 | PB1 (`STARTER_RELAY`) | out | **Not yet wired** — see "Known gaps" |
| Tri-color LED green | D9 | PA8 (`LED_GREEN`) | out | |
| AUX1 (lights, placeholder LED for now) | D1/TX | PA9 (`AUX1_OUT`) | out | |
| AUX2 (smoke, placeholder LED for now) | D0/RX | PA10 (`AUX2_OUT`) | out | Removed 2026-08-08 to save a pin on the handle, restored 2026-08-09 once pins freed up elsewhere on that board — never actually un-assigned here, so no CubeMX changes were needed. |
| Tri-color LED blue | D10 | PA11 (`LED_BLUE`) | out | |
| Radio SCK | D13 | PB3 (`SPI1_SCK`) | out | onboard LD3 flickers with SPI clock — harmless |
| Radio MISO | D12 | PB4 (`SPI1_MISO`) | in | |
| Radio MOSI | D11 | PB5 (`SPI1_MOSI`) | out | |

15 of 17 usable pins committed, 2 spare.

## Why no solder-bridge rework

`PA5` (servo) and `PA7`/`PA8`/`PA11` (LEDs) are all actively used, but none of
them share a bridge pair with anything else actively used: `PA5`'s pair
partner is `PB7`, and `PA6`'s pair partner is `PB6` — **neither `PB7` nor
`PB6` is used anywhere in this pin plan**. A bridge only causes a conflict
when *both* paired pins are actively driven; leaving one side genuinely
unused sidesteps the whole issue without touching the board. This was a
deliberate constraint when picking pins (see the project conversation this
doc was extracted from, and `DEVELOPMENT/radio/README.md`'s solder-bridge
note for the original discovery of this board quirk).

## Radio (nRF24L01+PA+LNA)

**Reused unchanged from `DEVELOPMENT/radio/spi-bringup/`** — same module,
same wiring, same decoupling caps (10–100µF electrolytic + 0.1µF ceramic at
the module's V+/GND). Don't rewire anything here; this is exactly the
bring-up setup that already proved a working two-board link. See
`DEVELOPMENT/radio/README.md` for the full bring-up history.

## Servo

**Currently an SG90 micro servo — bench-test placeholder, not the final
actuator.** See `docs/OPEN-ITEMS.md` "Servo selection": a fish scale is on
hand for measuring the real throttle cable pull force, which will pick a
proper metal-gear servo later. `prod_app.c`'s `SERVO_PULSE_MIN_US`/
`SERVO_PULSE_MAX_US` (1000–2000µs) may need retuning for that servo's
actual travel, but the 50Hz frame rate (`TIM2` Prescaler=31, Period=19999)
should hold for any standard-protocol servo.

3 wires, standard SG90 color code (same as the bench rig's):
- **Brown → GND**
- **Red → 5V** (the Nucleo's `5V` header pin, sourced from USB when
  connected via ST-Link) — **not** the `3V3` pin, same reasoning as the
  bench rig: the servo draws more current than that rail is sized for, and
  it needs 5V anyway. If the board resets/browns out when the servo moves,
  switch to a separate 5V supply, grounds still tied together.
- **Orange → A4 (PA5)** (signal)

## Tri-color status LED

**Confirmed common cathode** — verified directly: common (longest) pin to
GND, red pin through a 220Ω resistor to 3.3V, lit red. Matches what
`prod_app.c` already assumes (`GPIO_PIN_SET` = lit).

**Physical pin order on this specific package: B-G-C-R** (Blue, Green,
Common, Red, reading across the 4 leads in order — equivalently R-C-G-B
from the other end). The common (C) pin is the longest lead; it's the
*second* pin from the blue end, not an outer pin, so it's easy to
miscount by one under this specific part — check the long lead, not just
position, before wiring.

- Common (longest lead, 2nd from the blue end) → **GND**
- Red (outer pin, opposite end from blue) → 220Ω resistor → **A6 (PA7)**
- Green (between blue and common) → 220Ω resistor → **D9 (PA8)**
- Blue (outer pin, opposite end from red) → 220Ω resistor → **D10 (PA11)**

220Ω was chosen because it's already confirmed working on the red channel;
green/blue LEDs typically have a higher forward voltage than red, so they
may read visibly dimmer at the same resistor value — fine for a first pass,
worth individually tuning later if the brightness mismatch bothers you.

**Behavior** (see `prod_app.h` for the authoritative writeup): red blinks as
a heartbeat while armed (faster if the receiver's own battery is low),
solid red when killed; green lights only on a voluntary release from
cranking (never a forced stop); blue is on while actively cranking.

## Local start button

Ground-crew start override, independent of the wireless link (see
`start_tick()` in `src/receiver/receiver_firmware.c`). One leg to **D3
(PB0)**, other leg to **GND** — internal pull-up already enabled in CubeMX,
active-low (pressed = pin reads LOW), same convention as every other button
in this project. No hold-to-start timer, just debounced
(`INPUT_DEBOUNCE_MS`) — see the project's discussion for why (a deliberately
placed physical button on an installed unit, not something loose that could
get bumped).

## AUX1 / AUX2 (accessory outputs)

Eventually these will drive relays (AUX1 a strobe light circuit, AUX2 a
smoke circuit). **For now, each is wired as an individual indicator LED**
just to confirm the circuit is functioning — standard LED wiring (a plain
single LED, not part of the tri-color package):

- AUX1: **D1/TX (PA9)** → resistor (~220–330Ω) → LED anode → LED
  cathode → GND.
- AUX2: **D0/RX (PA10)** → resistor (~220–330Ω) → LED anode → LED
  cathode → GND.

The receiver treats both as plain level flags and doesn't know or care
that the handle latches AUX1 (toggle on/off) while leaving AUX2 purely
momentary — see `DEVELOPMENT/handle/handle-prod/docs/wiring.md` for that
distinction.

(AUX2 was removed 2026-08-08 to save a pin budget on the handle, then
restored 2026-08-09 once pins freed up elsewhere on that board.)

## Known gaps — deliberately left unconnected

These three pins are configured in CubeMX and referenced in firmware, but
**not yet wired to anything**, because each is blocked on information this
project doesn't have yet. Leaving them floating is electrically harmless —
they just won't do anything meaningful until wired.

- **Kill relay (A3/PA4)** and **starter relay (D6/PB1)**: a GPIO pin can't
  directly drive a relay coil (needs far more current than the ~20mA a GPIO
  can source) — each needs a transistor/MOSFET driver stage or an
  opto-isolated relay module in between. Per `docs/OPEN-ITEMS.md` ("Kill
  driver", "Starter driver"), the actual Vittorazi Moster 185 kill-wire
  voltage/behavior and starter solenoid coil voltage/current haven't been
  measured yet — needed to spec the driver circuit before wiring this for
  real.
- **Battery sense (A5/PA6)**: `read_battery_mv()` currently uses a 1:1
  placeholder (no divider) — **do not connect a real battery pack above
  ~3.3V to this pin** until a proper resistor divider is sized to the
  chosen pack's voltage (see `docs/OPEN-ITEMS.md` "Battery chemistry/
  voltage per pack"), or the pack voltage would be fed directly into the
  ADC pin and exceed its absolute maximum rating.

## Power rails: 3V3 vs 5V vs VIN

Same guidance as the bench rig (`DEVELOPMENT/receiver/docs/wiring.md`): use
**3V3** for the LED/logic-level rail, **5V** for the servo specifically
(sourced from USB via ST-Link), and don't use **VIN** at all — it's an
input for external power, unconnected while bench-powered over USB.
