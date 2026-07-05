# Throttle — drive-by-wire paramotor throttle

Open firmware (and, in time, hardware) for a **drive-by-wire paramotor throttle**.
A handle-mounted STM32 reads a trigger, kill switch, start button, cruise button
and accessory switches, and transmits 5-byte packets at 80 Hz over an nRF24L01+
radio to a second STM32 near the engine. The receiver validates every packet,
runs a kill/start/run state machine, and drives a servo that pulls the physical
throttle cable — with an independent loss-of-signal watchdog that ramps the
engine back to idle if the link drops.

> [!WARNING]
> ## Safety disclaimer — read this
>
> This is **DIY, safety-critical hardware**. It controls the throttle and
> ignition of a running two-stroke engine strapped to a person in flight.
>
> - It is **not certified** to any aviation or functional-safety standard, and
>   makes no claim of airworthiness.
> - You build, wire, configure, and fly it **entirely at your own risk**.
> - The firmware is provided **"AS IS", with no warranty** of any kind (see
>   `LICENSE`).
> - The design depends on a **mechanical kill switch wired directly into the
>   ignition line, independent of this firmware**. Do not remove it. The
>   authors accept no responsibility for injury or damage arising from use of
>   this project, from forks or modifications, or from removal or defeat of any
>   safety feature (mechanical kill, loss-of-signal watchdog, fail-safe wiring).
>
> If you intend to build and fly this, having someone familiar with the legal
> and liability implications of open safety-critical hardware review your plans
> is strongly advised.

## Status — not flight-ready

This project is **incomplete** and under active development. Known gaps that are
safety-relevant:

- **Engine-caught detection (`STARTING` → `RUNNING`) is not implemented.** The
  signal that tells the receiver the engine actually started (RPM sense,
  vibration, or a timed window) has not been chosen or built. **Do not fly**
  assuming the start sequence is finished.
- The firmware `.c` files are **HAL/RF24 stubs** — peripheral inits, the radio
  port, ADC/GPIO/PWM, and battery voltage divider values are placeholders that
  must be filled in during board bring-up.
- Timing/threshold constants are bench-tuning starting points, not validated.

See `docs/OPEN-ITEMS.md` for the full living list of unresolved questions and
action items, and `docs/PROJECT_DESIGN.md` for the rationale behind the design.

## Layout

```
src/common/     shared contract: packet protocol, CRC8, battery monitor (must stay identical both ends)
src/handle/     transmitter firmware (trigger, kill, start, cruise, accessories, battery)
src/receiver/   receiver firmware (validation, state machine, servo, watchdog, battery)
test/           host-side pure-logic tests (no hardware needed)
hardware/       schematics, PCB, mechanical, BOM (to come)
docs/           design doc, open items, architecture decision records
```

## Build & test

There is **no committed embedded build system yet** — building the firmware for
real requires generating an STM32CubeIDE project, wiring the peripheral inits,
and linking a HAL port of RF24 (TMRh20).

The pure logic (CRC8, sequence rollover, battery mapping, cruise, kill debounce)
can be tested on your PC with any C compiler:

```sh
# host-side logic tests
gcc -Wall -Wextra -Isrc/common -Itest test/test_logic.c -o test_logic && ./test_logic

# compile-check the firmware (stubs; no main/hardware)
gcc -c -Wall -Wextra -Isrc/common src/handle/handle_firmware.c    -o handle.o
gcc -c -Wall -Wextra -Isrc/common src/receiver/receiver_firmware.c -o receiver.o
```

## License

- **Firmware / software:** GNU General Public License v3.0-or-later — see
  `LICENSE`. Derivatives that are distributed must remain open under the same
  terms.
- **Hardware** (schematics, PCB, mechanical designs, once added): will be
  licensed under **CERN-OHL-S** (strongly reciprocal). See `hardware/README.md`.
