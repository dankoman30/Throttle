# DEVELOPMENT/handle/handle-prod — production handle/transmitter

Runs the **real** handle kill-latch/cruise/start-hold logic
(`src/handle/handle_firmware.c`, unmodified except small, reviewed hardware
fill-ins guarded by `HANDLE_PROD_BOARD` — see `Src/handle_app.c`'s header
comment) on an STM32L432KC (Nucleo-32), driving **real hardware**: the
trigger position sensor, kill/start/cruise/AUX1 inputs, the nRF24L01+
radio TX, and a tri-color status LED. Compare
`DEVELOPMENT/receiver/receiver-prod/`, the same `#include`-reuse pattern
applied to the receiver side.

## Layout

```
docs/
  wiring.md         pin-by-pin connections, resistor values, known wiring gaps
Inc/handle_app.h     status LED color/priority behavior
Src/handle_app.c      real GPIO/ADC ingestion + radio TX + LED actuation
handle-prod.ioc       STM32CubeMX project file
```

Full STM32CubeMX-generated project (HAL/CMSIS drivers under `Drivers/`,
`STM32CubeIDE/.project`/`.cproject`) targeting NUCLEO-L432KC, with
`handle_app.c`/`handle_app.h` wired into `main.c`. Same linked-resource
caveat as receiver-prod: this project's `.project` tracks source files as
an explicit list, not a live directory scan — a new file in `Src/`/`Inc/`
needs a manually-added `<link>` entry or the IDE won't see it.

## Board

Reuses "board A" from `DEVELOPMENT/radio/spi-bringup/` — its radio wiring
(`NRF_CE`=`PA1`, `NRF_CSN`=`PA3`, SPI1 on `PB3`/`PB4`/`PB5`) already
matched this project's pin plan exactly, so it carried over unchanged;
everything else (trigger, kill/start/cruise/AUX1, status LED) was wired
fresh on top. Same SB16/SB18-avoidance reasoning as receiver-prod - see
`docs/wiring.md` for the full pin table and why it works.

## Status

- **2026-08-08: full command path confirmed on real hardware** — radio
  uplink to a receiver board, trigger position driving the servo live over
  the air, the start button correctly showing the cranking indication,
  cruise engage/disengage, and AUX1, all working end-to-end. Two real bugs
  were caught and fixed live during bring-up, both worth remembering:
  - **Cruise and AUX1 wired to GND instead of 3V3.** Those two inputs use
    the opposite pull convention from kill/start (pull-down vs pull-up),
    so their "other leg" needs to go to 3V3 - wiring all four buttons to
    GND by habit meant cruise/AUX1 could never read HIGH, so neither
    button did anything until corrected. See `docs/wiring.md`'s pin table
    note.
  - **Trigger ADC pull-down initially wired in series instead of
    parallel** - spliced into the wiper wire itself rather than as a
    separate leg to GND, which wouldn't have provided the intended
    fail-safe protection (the pin would still float if that wire broke)
    and would have starved the ADC's short sampling time. Corrected before
    it caused a real symptom.
- **Known, deliberate gap: the kill switch is currently a normally-open
  bench stand-in**, bridged closed with a jumper wire to unblock testing
  everything else. This design requires a genuinely normally-closed
  switch/contact block - swapping it is tracked in `docs/OPEN-ITEMS.md`.
- **Not yet measured**: real trigger ADC min/max at full release/pull (see
  `docs/wiring.md` "Trigger"); handle battery power source (sensing was
  dropped entirely, but the board still needs a real pack for field use).
