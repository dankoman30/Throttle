# Receiver bench schematic

No KiCad files yet (see `hardware/README.md` — that's where real schematics
will eventually live under CERN-OHL-S). This is a breadboard-level diagram
for the bench rig only, sufficient to wire it up; see `../docs/wiring.md`
for exact pin names and resistor values.

**Before wiring anything to `A4`/`A5`/`D4`/`D5`: cut solder bridges `SB16`
and `SB18` on the PCB.** They're closed by default on this board revision
and internally tie `PA6`↔`PB6` and `PA5`↔`PB7` together — see "Required
board mod" in `../docs/wiring.md` for the full explanation and how-to.

## Block diagram

Pin labels below are **Arduino header label / STM32 pin** — see
`../docs/wiring.md` for the full translation table and resistor-value
derivations. Renders natively on GitHub and in most Markdown viewers — no
extra software needed. (An ASCII version of this diagram lived here
previously; superseded by this Mermaid version, kept in git history if
ever needed.)

```mermaid
flowchart LR
    classDef power fill:#3a3a3a,stroke:#888,color:#fff
    classDef mcu fill:#1f4e79,stroke:#0d2b45,color:#fff
    classDef input fill:#2e7d32,stroke:#1b4d1e,color:#fff
    classDef output fill:#8a4b08,stroke:#5c3205,color:#fff
    classDef passive fill:#555,stroke:#222,color:#fff,stroke-dasharray: 3 3

    V33["3V3 rail"]:::power
    V5["5V rail"]:::power
    GND(["GND — common return"]):::power

    subgraph MCU["STM32L432KC Nucleo-32"]
        direction TB
        A1["A1 / PA1\nADC1_IN6"]:::mcu
        A2["A2 / PA3"]:::mcu
        A3["A3 / PA4"]:::mcu
        A4["A4 / PA5\nTIM2_CH1 PWM"]:::mcu
        A5["A5 / PA6"]:::mcu
        SEGPINS["A6, D9, D1/TX, D0/RX,\nD10, D2, D3\n(segs a-g)"]:::mcu
        D6["D6 / PB1"]:::mcu
        D12["D12 / PB4"]:::mcu
        D11["D11 / PB5"]:::mcu
        D5PIN["D5 / PB6"]:::mcu
        D4PIN["D4 / PB7"]:::mcu
    end

    POT["B103 pot\n10k linear, wiper center"]:::input
    BTN_START(("Start\nbutton")):::input
    BTN_CRUISE(("Cruise\nbutton")):::input
    BTN_KILL(("Kill\nbutton")):::input
    SERVO["SG90 servo"]:::output
    SEG["5611AH 7-seg\n(common-cathode)"]:::output
    BUZZ["Piezo buzzer"]:::output
    LED_G(("Green LED")):::output
    LED_R(("Red LED")):::output
    LED_Y(("Yellow LED")):::output
    LED_H(("Heartbeat LED")):::output
    R_G["220-330Ω"]:::passive
    R_R["220-330Ω"]:::passive
    R_Y["220-330Ω"]:::passive
    R_H["220-330Ω"]:::passive

    V33 --- POT
    GND --- POT
    POT -- wiper --> A1

    A2 -. "internal pull-up" .-> BTN_START --> GND
    A3 -. "internal pull-up" .-> BTN_CRUISE --> GND
    A5 -. "internal pull-up" .-> BTN_KILL --> GND

    A4 -- "orange: signal" --> SERVO
    V5 -- "red: V+" --> SERVO
    SERVO -- "brown: GND" --> GND

    SEGPINS -- "7x 150-220Ω\n(one per segment)" --> SEG
    SEG -- "common cathode" --> GND

    D6 -- click/pop --> BUZZ --> GND

    D12 --> R_G --> LED_G --> GND
    D11 --> R_R --> LED_R --> GND
    D5PIN --> R_Y --> LED_Y --> GND
    D4PIN --> R_H --> LED_H --> GND
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
