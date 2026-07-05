# 0006 — Firmware framework: STM32 HAL + CubeIDE

**Status:** accepted · **Date:** 2026

## Decision

Build the firmware on **STM32 HAL via STM32CubeIDE** (with CubeMX for peripheral
config), for both handle and receiver — not the Arduino core (STM32duino).
Prototype on a **Nucleo-32 STM32L432KC** dev board (Nucleo-64 if the pin/
peripheral count needs it); move to a smaller custom footprint for production.

## Why

- The developer wants to learn STM32 properly and have full control over
  peripherals/timing, which matters for a safety-critical control loop.
- The existing firmware stubs already assume HAL conventions (`HAL_GetTick`,
  `HAL_ADC_GetValue`, `HAL_GPIO_*`, `HAL_TIM_PWM_*`).
- CubeMX gives a clear pin-mux / clock-config GUI, easing the STM32 learning
  curve for someone coming from Arduino.

## Consequence

- The **RF24 (TMRh20) → HAL port** stays on the critical path: RF24 is
  Arduino-first, so the SPI/CE/CSN/IRQ layer must be adapted to HAL. (Choosing
  the Arduino core would have removed this task — deliberately not taken.)
- Pure logic (protocol, CRC8, watchdog, cruise, kill debounce, battery mapping)
  is framework-agnostic and already lives in `src/common/` + the `.c` files;
  only the thin hardware wrappers marked `/* fill in */` are HAL-specific.

## Rejected alternatives

- **STM32duino (Arduino core):** gentler on-ramp, RF24 works natively, but less
  low-level control and a framework the developer specifically wants to move
  beyond. Reasonable as a fallback if HAL bring-up stalls.
