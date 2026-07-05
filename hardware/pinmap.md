# Pin map / peripheral allocation

Derived from the firmware in `src/`. Target dev board: **Nucleo-32 STM32L432KC**
(one per unit). This is the blueprint you click into CubeMX.

> **How to read this:** allocation is by **peripheral + function** — that's what
> actually constrains the design. Exact pin numbers are assigned in **CubeMX**,
> which shows alternate-function conflicts live and prevents illegal
> combinations. The "suggested pin" column is a *starting point* to validate
> there, not a final assignment. SWD debug pins (PA13/PA14) are reserved by the
> Nucleo's on-board ST-Link — don't reuse them.

Both units share the SPI radio interface and the battery monitor; they differ in
the rest of their I/O.

## Shared — nRF24L01+ radio (both units)

| Function | Peripheral | Suggested pin | Notes |
|---|---|---|---|
| SCK  | SPI1_SCK  | PA5 | SPI clock |
| MISO | SPI1_MISO | PA6 | |
| MOSI | SPI1_MOSI | PA7 | |
| CSN  | GPIO out  | PB0 | SPI chip-select (RF24 `csn`) |
| CE   | GPIO out  | PB1 | RF24 chip-enable (TX/RX toggle) |
| IRQ  | GPIO in (EXTI) | PB2 | **Optional** — our code polls `radio.available()`; wire it only if you move to interrupt-driven RX |

nRF24L01+PA+LNA draws current bursts on TX (~100+ mA) — give it a solid 3.3 V
rail with local bulk + ceramic decoupling right at the module, or it browns out
mid-transmit.

## Shared — battery monitor (both units, own pack only)

| Function | Peripheral | Suggested pin | Notes |
|---|---|---|---|
| Battery sense | ADC1_INx | PA4 | Through a divider; see `battery_monitor.h`. Set divider ratio + profile mV per pack |
| LED bar (3–4) | GPIO out ×4 | PB3, PB4, PB5, PB6 | Lights from power-on |
| Piezo buzzer  | GPIO out (or TIM) | PB7 | Low-batt beep; a TIM PWM pin lets you drive a tone rather than a DC buzzer |

## Handle unit (transmitter)

| Function | Peripheral | Suggested pin | Notes |
|---|---|---|---|
| Trigger position | ADC1_INx | PA0 | 12-bit, filtered in firmware. **Pull-down** so a broken trigger reads idle (ADR 0003) |
| Kill switch  | GPIO in | PA1 | **Fail-safe: normally-closed + pull-up**, open/broken = kill (ADR 0003) |
| Start button | GPIO in | PA2 | Normally-open, closed = start |
| Cruise button| GPIO in | PA3 | Normally-open, closed = press |
| Aux1 (lights)| GPIO in | PA8 | Normally-open, closed = on |
| Aux2 (smoke) | GPIO in | PA9 | Normally-open, closed = on |

Handle pin tally ≈ 6 (radio) + 6 (battery) + 6 (inputs) = **~18 of ~26 usable
GPIO** → fits the Nucleo-32 comfortably.

## Receiver unit (engine side)

| Function | Peripheral | Suggested pin | Notes |
|---|---|---|---|
| Servo signal | TIM2_CH1 (PWM) | PA0 | 50 Hz servo PWM; map 0–255 → pulse width |
| Battery sense | ADC1_INx | PA4 | Receiver's **dedicated** pack |
| Tach input (RPM) | TIM_CHx input capture | PA1 | From plug-lead inductive pickup — **needs a conditioning circuit** (clamp/TVS + series R + squaring to a clean 3.3 V logic edge) before this pin. One pulse per spark |
| Kill driver | GPIO out | PB8 | Drives **relay/opto** that grounds the CDI kill wire (parallels mechanical kill). Energize-to-kill |
| Starter driver | GPIO out | PB9 | Drives **relay/opto** to the engine-battery starter solenoid coil (flyback diode across coil). Bounded pulse + cooldown in firmware |
| Aux1 out (lights) | GPIO out | PB10 | Via driver sized to the load |
| Aux2 out (smoke)  | GPIO out | PB11 | Via driver sized to the load |

Receiver pin tally ≈ 5–6 (radio) + 6 (battery) + 7 (servo/tach/kill/starter/aux)
= **~18–19 GPIO** → also fits the Nucleo-32.

## Conclusion

**Nucleo-32 STM32L432KC is sufficient for both units.** No need to step up to a
Nucleo-64 on pin count. Bring-up order suggestion: radio link first (prove
handle→receiver packets), then servo, then battery monitor, then the engine
interface (kill/starter/tach) last — that one wants isolation hardware and the
engine present.

### Engine-interface hardware still to spec (see docs/OPEN-ITEMS.md)
- Isolation device choice for kill + starter (relay vs opto-SSR).
- CDI kill-wire voltage/behavior on the actual Moster 185.
- Starter solenoid coil voltage/current (sizes the driver + flyback).
- Tach-conditioning circuit component values.
- Servo selection (from cable-force measurement) → receiver power budget / BEC.
