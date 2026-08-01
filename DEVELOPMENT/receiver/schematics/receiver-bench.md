# Receiver bench schematic

No KiCad files yet (see `hardware/README.md` — that's where real schematics
will eventually live under CERN-OHL-S). This is a breadboard-level diagram
for the bench rig only, sufficient to wire it up; see `../docs/wiring.md`
for exact pin names, resistor values, and the 7-segment polarity test.

## Block diagram

```
                        +-------------------------------+
                        |     STM32L432KC Nucleo-32      |
                        |                                |
   B103 pot ----------->| PA1  (ADC1_IN2)                |
   (wiper; ends to       |                                |
    3V3 / GND)           |                                |
                        |                                |
   Start button -------->| PA2  (GPIO in, pull-up)        |
   (other leg -> GND)    |                                |
   Cruise button ------->| PA3  (GPIO in, pull-up)        |
   (other leg -> GND)    |                                |
   Kill button --------->| PA4  (GPIO in, pull-up)        |
   (other leg -> GND)    |                                |
                        |                                |
                        | PA0  (TIM2_CH1, PWM) ---------->|--[servo signal]
                        |                                |     SG90 servo
   5V header ----------->|--------------------------------|---> [servo V+]
   GND -----------------> |--------------------------------|---> [servo GND]
                        |                                |
                        | PA5..PA11 (7 x GPIO out) ------->|--[R]--[seg a..g]--+
                        |                                |                    |
                        |                                |     common --------+
                        |                                |     (3V3 if anode,
                        |                                |      GND if cathode)
                        |                                |
                        | PB0  (GPIO out) --------------->|--[piezo buzzer]--GND
                        | PB1  (GPIO out) --------------->|--[R]--[LED green]--GND
                        | PB2  (GPIO out) --------------->|--[R]--[LED red]----GND
                        | PB3  (GPIO out) --------------->|--[R]--[LED yellow]-GND
                        | PB4  (GPIO out) --------------->|--[R]--[LED heartbeat]-GND
                        |                                |
                        | GND ---------------------------- common ground rail
                        +-------------------------------+
```

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
