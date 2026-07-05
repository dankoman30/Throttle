# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Firmware for a **safety-critical drive-by-wire paramotor throttle**. A handle-mounted STM32 reads a trigger + kill switch + start button and transmits 5-byte packets at 80Hz over an nRF24L01+ radio to a remote STM32 near the engine, which validates them and drives a servo that pulls the physical throttle cable.

Read `PROJECT_DESIGN.md` first — it holds the rationale (why no radio-level ack, why CRC8/MAXIM, watchdog thresholds, open hardware questions) that the code comments assume.

## Build / test

There is **no committed build system** — no CMake, Makefile, or STM32CubeIDE `.project`. The `.c` files are HAL/RF24 stubs: `millis()` returns 0, ADC/GPIO/radio calls are commented out, and there is no `main()` entry point (functions are named `handle_firmware_main` / `receiver_firmware_main`). Building requires generating an STM32CubeIDE project, wiring up the peripheral inits marked `/* fill in */`, and linking against a HAL port of RF24 (TMRh20).

To sanity-check the pure-logic pieces without hardware, compile a host-side test (e.g. verify the CRC8 vector — `crc8_compute("123456789", 9)` must equal `0xA1`, and `seq_is_newer` rollover handling):

```sh
gcc -DTEST -I. your_test.c && ./a.out    # no test harness exists yet; write one against crc8.h / the seq logic
```

## Architecture and the invariants that matter

Two independent MCUs share one contract: **`throttle_protocol.h`** (packet layout + every timing constant) and **`crc8.h`**. Both files must stay byte-identical on both ends — changing the packet struct or a `#define` affects handle and receiver together. All timing/threshold values are deliberately **compile-time constants**, tuned on the bench, never runtime-adjustable.

- `handle_firmware.c` — transmitter. Reads inputs, debounces, enforces hold-to-start (`START_HOLD_REQUIRED_MS`), builds `throttle_packet_t`, sends at `HANDLE_TX_RATE_HZ`. Send-only radio (`stopListening`).
- `receiver_firmware.c` — receiver + servo driver + state machine + loss-of-signal watchdog.

**Safety ordering is the core design and must be preserved when editing the receiver:**

1. **Packet validation** (`on_packet_received`): length → sync byte → CRC8 → sequence-newer check. Any failure discards the whole packet.
2. **Command handling** (`handle_valid_packet`), kill always first: **KILL** (cut ignition, → `STATE_KILLED`, ignore rest of packet) → **KILLED is sticky** (no wireless field clears it; only a physical re-arm) → **START** (only from `IDLE_SAFE`, only if throttle ≤ `IDLE_THRESHOLD_FOR_START`, only if not recovering from loss) → **THROTTLE** (rate-limited via `step_toward_target`, suppressed during recovery).
3. **Watchdog** (`watchdog_tick`, runs independently of packet arrival): threshold A ramps throttle linearly to idle, threshold B forces idle + marks link recovering; after packets resume the link must be continuously valid for `LINK_RESTORE_STABLE_MS` before pilot throttle input is honored again.

When touching the receiver, do not reorder these, do not let a wireless path clear `STATE_KILLED`, and keep the watchdog independent of the packet-receive path. The **mechanical kill switch is intentionally absent from the firmware** — it is wired directly into the ignition line so a software hang cannot block it; don't add it to the code.
