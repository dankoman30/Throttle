# Receiver bench schematic

No KiCad files yet (see `hardware/README.md` — that's where real schematics
will eventually live under CERN-OHL-S). This is a breadboard-level diagram
for the bench rig only, sufficient to wire it up; see `../docs/wiring.md`
for exact pin names and resistor values.

## Block diagram

Pin labels below are **Arduino header label (STM32 pin)** — see
`../docs/wiring.md` for the full translation table.

```
                        +-------------------------------+
                        |     STM32L432KC Nucleo-32      |
                        |                                |
   B103 pot ----------->| A1   (PA1, ADC1_IN6)           |
   (wiper; ends to       |                                |
    3V3 / GND)           |                                |
                        |                                |
   Start button -------->| A2   (PA3, GPIO in, pull-up)   |
   (other leg -> GND)    |                                |
   Cruise button ------->| A3   (PA4, GPIO in, pull-up)   |
   (other leg -> GND)    |                                |
   Kill button --------->| A5   (PA6, GPIO in, pull-up)   |
   (other leg -> GND)    |                                |
                        |                                |
                        | A4   (PA5, TIM2_CH1, PWM) ----->|--[orange: servo signal]
                        |                                |     SG90 servo
   5V header ----------->|--------------------------------|---> [red: servo V+]
   GND -----------------> |--------------------------------|---> [brown: servo GND]
                        |                                |
                        | A6,D9,D1/TX,D0/RX,D10,D2,D3    |
                        | (7 x GPIO out, segs a..g) ----->|--[R]--[seg a..g]--+
                        |                                |                    |
                        |                                |     common --------+
                        |                                |     (GND - confirmed
                        |                                |      common-cathode)
                        |                                |
                        | D6   (PB1,  GPIO out) --------->|--[piezo buzzer]--GND
                        | D12  (PB4,  GPIO out) --------->|--[R]--[LED green]--GND
                        | D11  (PB5,  GPIO out) --------->|--[R]--[LED red]----GND
                        | D5   (PB6,  GPIO out) --------->|--[R]--[LED yellow]-GND
                        | D4   (PB7,  GPIO out) --------->|--[R]--[LED heartbeat]-GND
                        |                                |
                        | GND ---------------------------- common ground rail
                        +-------------------------------+
```

**Remove the factory spare jumper cap between GND and D2 first** — it's
not functional, just a parking spot ST ships a spare clip in, but D2
(PA12) is segment f here and the jumper would short it to ground. See
`../docs/wiring.md`.

**Deliberately unused** (reserved by the board — see `../docs/wiring.md`):
A0/PA0 (board-locked MCO/clock-in), A7/PA2 (ST-Link VCP_TX), PA13/PA14
(SWD, not on this header), PA15 (ST-Link VCP_RX, not on this header),
D13/PB3 (on-board green LED, LD3), D7/D8 (PC14/PC15, crystal). PB2 doesn't
exist on this package at all and has no Arduino label.

`[R]` = one current-limiting resistor per segment/LED (~220–330Ω, see
`../docs/wiring.md`). All GND connections (buttons, pot, display common if
cathode, buzzer, LEDs, servo) share one breadboard ground rail tied to the
Nucleo's GND.

## 7-segment segment layout (for reference)

```
        aaaa
       f    b
       f    b
        gggg
       e    c
       e    c
        dddd
```

`bench_app.c`'s `DIGIT_SEGMENTS` table encodes which of a–g light for each
digit 0–9; the decimal point is left unconnected (not used for a 0–9
readout).

## Servo power note

Route the SG90's V+ from the Nucleo's `5V` header pin (USB-derived), not
`3V3`. If the board resets when the servo moves under load, switch the
servo to a separate 5V supply and keep grounds common — see
`../docs/wiring.md`.

## Power rail note

The pot/display-common rail is `3V3`, not `VIN` (VIN is a power *input* for
an external battery/adapter, not a usable output while running on USB) and
not `5V` (the pot feeds the ADC directly, which is only rated to ~3.3V) —
see "Power rails: 3V3 vs 5V vs VIN" in `../docs/wiring.md` for the full
reasoning.
