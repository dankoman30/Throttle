# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Firmware for a **safety-critical drive-by-wire paramotor throttle**. A handle-mounted STM32 reads a trigger + kill switch + start button and transmits 5-byte packets at 80Hz over an nRF24L01+ radio to a remote STM32 near the engine, which validates them and drives a servo that pulls the physical throttle cable.

Read `docs/PROJECT_DESIGN.md` first — it holds the rationale (why no radio-level ack, why CRC8/MAXIM, watchdog thresholds, open hardware questions) that the code comments assume. `docs/decisions/` has short ADRs for the load-bearing choices, and `docs/OPEN-ITEMS.md` tracks unresolved/safety-relevant work (e.g. engine-caught detection is not implemented — the project is not flight-ready).

## Layout

`src/common/` — shared contract (`throttle_protocol.h`, `crc8.h`, `led_blink.h`), compiled into both ends and must stay byte-identical. `src/handle/` — transmitter. `src/receiver/` — receiver. `test/` — host-side logic tests. `hardware/`, `docs/` — designs and docs. `.claude/agents/` — `safety-reviewer` and `protocol-guardian` review subagents.

## Build / test

There is **no committed embedded build system** — no CMake, Makefile, or STM32CubeIDE `.project`. The `.c` files are HAL/RF24 stubs: `millis()` returns 0, ADC/GPIO/radio calls are commented out, and there is no `main()` entry point (functions are named `handle_firmware_main` / `receiver_firmware_main`). Building for real requires generating an STM32CubeIDE project, wiring up the peripheral inits marked `/* fill in */`, and linking against a HAL port of RF24 (TMRh20).

The pure logic has a host-side test harness (`test/test_logic.c`) — CRC8 vector (`0xA1`), sequence rollover, cruise, and kill debounce. Shared headers live in `src/common/`, so every build needs `-Isrc/common`:

```sh
# host-side logic tests
gcc -Wall -Wextra -Isrc/common -Itest test/test_logic.c -o test_logic && ./test_logic   # -> ALL TESTS PASSED

# compile-check the firmware stubs (expected warning: on_packet_received unused — only called from commented radio block)
gcc -c -Wall -Wextra -Isrc/common src/handle/handle_firmware.c    -o handle.o
gcc -c -Wall -Wextra -Isrc/common src/receiver/receiver_firmware.c -o receiver.o
```

Cruise + kill-debounce logic lives in static functions inside `handle_firmware.c`; the test mirrors them (kept in lockstep by hand) to test without the HAL stubs.

## Architecture and the invariants that matter

Two independent MCUs share one contract in `src/common/`: **`throttle_protocol.h`** (packet layout + every timing constant) and **`crc8.h`**. Both files must stay byte-identical on both ends — changing the packet struct or a `#define` affects handle and receiver together (the `protocol-guardian` subagent checks this). All timing/threshold values are deliberately **compile-time constants**, tuned on the bench, never runtime-adjustable.

- `src/handle/handle_firmware.c` — transmitter. Reads inputs, debounces, enforces hold-to-start (`START_HOLD_REQUIRED_MS`), builds `throttle_packet_t`, sends at `HANDLE_TX_RATE_HZ`. Send-only radio (`stopListening`). **Kill is latched here** (`g_kill_latched`): once the switch is seen active (fail-safe: open/broken wire = kill, debounced by `KILL_DEBOUNCE_MS`), every subsequent packet carries `CMD_FLAG_KILL` so a brief press survives dropped packets in this ack-less link; it also suppresses start-request and cruise in the same packet. Clearing it requires a re-arm (power cycle). **Cruise** is resolved here too (`apply_cruise`): it freezes the transmitted throttle at a setpoint, so the receiver stays cruise-transparent and the watchdog still overrides it. The mechanical kill line is the backup for a dead radio.
- `src/receiver/receiver_firmware.c` — receiver + servo driver + state machine + loss-of-signal watchdog. Also reads a local start button (ground-crew override, works even with zero wireless packets ever received) via `start_tick()`. Accessory outputs (`apply_aux_outputs`) are non-safety and kept out of the state machine.

**Safety ordering is the core design and must be preserved when editing the receiver:**

1. **Packet validation** (`on_packet_received`): length → sync byte → CRC8 → sequence-newer check. Any failure discards the whole packet.
2. **Command handling** (`handle_valid_packet`), kill always first: **KILL** (cut ignition, → `STATE_KILLED`, ignore rest of packet, also clears `g_wireless_start_req`) → **KILLED is sticky** (no wireless field clears it; only a physical re-arm) → **START** (just records the latest `CMD_FLAG_START_REQ` into `g_wireless_start_req` here — the actual gating and transition happen in `start_tick`, below) → **THROTTLE** (rate-limited via `step_toward_target`, suppressed during recovery, otherwise applied every valid packet regardless of state — including while `STATE_STARTING`, so the pilot can open the throttle mid-crank to help a cold engine catch).
3. **Start gating** (`start_tick`, runs independently of packet arrival — like the watchdog below, this is what lets the local button work with the wireless link fully down): ORs `g_wireless_start_req` with the debounced local button into one signal, edge-triggers `begin_crank`/`end_crank` off it (only from `IDLE_SAFE`, only if `g_target_throttle` ≤ `IDLE_THRESHOLD_FOR_START`, only if not recovering from loss, not during starter cooldown — same gates the old inline logic used). `begin_crank` records whether wireless was involved in *that* crank's trigger (`g_crank_is_local_only`); `crank_tick`'s loss-of-signal abort (`CRANK_LOSS_ABORT_MS`) checks that recorded flag, not the local button's live reading, specifically so a stuck/faulty local reading can't silently defeat the abort for a wireless-triggered crank.
4. **Watchdog** (`watchdog_tick`, runs independently of packet arrival): threshold A ramps throttle linearly to idle, threshold B forces idle + marks link recovering; after packets resume the link must be continuously valid for `LINK_RESTORE_STABLE_MS` before pilot throttle input is honored again.

When touching the receiver, do not reorder these, do not let a wireless path clear `STATE_KILLED`, and keep the watchdog and start-gating ticks independent of the packet-receive path. The **mechanical kill switch is intentionally absent from the firmware** — it is wired directly into the ignition line so a software hang cannot block it; don't add it to the code. There is also no local kill button by design — the physical unit's master kill switch fills that role directly in hardware, same as the mechanical kill line.
