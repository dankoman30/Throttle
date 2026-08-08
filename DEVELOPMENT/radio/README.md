# DEVELOPMENT/radio — RF24 HAL port & radio link bring-up

**Status: all hardware has arrived (2026-08-06). Nothing built yet — starting
bring-up now.** This doc exists so picking this up doesn't require
re-deriving context from scratch.

## What this covers

Bringing up the actual nRF24L01+ link between `handle` and `receiver`. Both
`src/handle/handle_firmware.c` and `src/receiver/receiver_firmware.c` currently
have the entire radio path commented out (`radio.begin()`, `radio.write()`,
`radio.available()`/`radio.read()`, etc.) — the packet/state-machine logic on
both ends is already proven (`DEVELOPMENT/receiver/`), but nothing has actually
gone over the air yet. `docs/OPEN-ITEMS.md` flags the RF24 HAL port as staying
"on the critical path" for exactly this reason.

## Hardware inventory

**All arrived:**
1. **4x nRF24L01+PA+LNA modules**, from [Addicore](https://www.addicore.com/products/nrf24l01-pa-lna-with-antenna-2-4ghz-wireless-transceiver)
   (2 for the link + 2 spares). Clone chips (silkscreened as genuine Nordic
   but actually `SI24R1`/Beken parts) are a known problem for this exact
   module category and can't be reliably told apart by inspection (Nordic is
   fabless; markings vary by fab run) — buying multiple units from one
   reputable, consistent source is the practical mitigation, per ADR 0005's
   "not clone boards" requirement.
2. **2 additional STM32L432KC Nucleo-32 boards**, ordered directly from ST.
   Combined with the two already on hand, that's **4 total boards — only 1
   (the current receiver bench rig) has SB16/SB18 removed.** The other
   existing board and both new ones are stock/unmodified. See the SPI pin
   mapping note below before wiring any of the three unmodified boards for
   radio — it's directly relevant.
3. **Decoupling capacitors**, from Amazon:
   - ALLECIN 24-value electrolytic capacitor assortment kit (0.1µF–1000µF,
     10V/16V/25V/50V). Use the **16V or 25V** parts for this (not the 10V
     ones) — margin above the 3.3V rail, per the reasoning below.
   - 24-value / 480pc multilayer ceramic capacitor assortment kit
     (10pF–10µF) — covers the needed 0.1µF (100nF) many times over.
   - At each module's V+/GND pins, as close to the module as physically
     possible: one bulk **10–100µF** (from the electrolytic kit, 16V/25V) +
     one **0.1µF ceramic** (from the ceramic kit). **Known failure mode
     without this**: the PA+LNA variant draws real current spikes on TX
     (order ~100+mA) that a Nucleo's small onboard 3.3V LDO isn't sized for —
     one of the most common causes of "the radio just doesn't work / works
     intermittently" reports for this module. If it's still flaky with local
     decoupling, move to a dedicated 3.3V supply rather than the Nucleo's
     onboard rail.
4. **LONELY BINARY 8-Channel 24MHz USB Logic Analyzer Kit** (Amazon, ~$20;
   includes base module, breadboard breakout board, USB-A + Type-C cables,
   test clips, alligator clips). This is the same class of Cypress
   FX2LP-based hardware behind most "Saleae Logic clone" listings — pair it
   with **sigrok / PulseView** (free, open source) via its `fx2lafw` driver —
   worth doing a first smoke test now (confirm PulseView detects it) rather
   than waiting until it's needed mid-bring-up. PulseView has a **built-in
   SPI protocol decoder**, so feeding it
   SCK/MOSI/MISO/CSN shows decoded byte-level SPI transactions directly, not
   just raw waveforms — exactly what's needed to check nRF24 register
   reads/writes against the datasheet. Already on the toolchain wishlist in
   `docs/PROJECT_DESIGN.md`; would have shortened the SB16/SB18 debugging
   saga considerably had we had register-level SPI visibility instead of
   just GPIO reads. **Caution**: these clones typically have minimal input
   protection and are 3.3V-logic-only — fine for this SPI work (everything
   here is 3.3V) but don't probe 5V signals (e.g. the servo's 5V rail) with
   it.

No level shifting needed for the radio modules either way — their SPI inputs
are 5V-tolerant and the onboard regulator accepts 1.9–3.6V, a direct match
for the L432KC's 3.3V logic.

## SPI pin mapping: use PB3/PB4/PB5, not the PA5/PA6/PA7 default

**Important given 3 of the 4 boards don't have SB16/SB18 removed.** The
STM32L432KC's *default* SPI1 alternate-function mapping is `PA5`=SCK,
`PA6`=MISO, `PA7`=MOSI (AF5) — which is exactly the pin pair SB16/SB18
bridge to `PB6`/`PB7`. Using the default mapping on any unmodified board
would reproduce the same "two header pins are secretly one net" problem that
caused the kill-button saga, except manifesting as corrupted/garbled SPI
transactions rather than a flat stuck signal — likely *harder* to diagnose.

STM32L432KC also supports an **alternate SPI1 mapping on `PB3`/`PB4`/`PB5`**
(also AF5). **Use that mapping in CubeMX for the initial bring-up boards** —
it sidesteps the solder-bridge pins entirely, so none of the 3 unmodified
boards need SB16/SB18 removed just to get SPI working. One cosmetic note:
`PB3` is wired to the onboard LED `LD3`, so it'll flicker with SPI clock
activity — harmless, not a real conflict.

**Correction: this does not solve the receiver bench rig's pin problem** —
an earlier draft of this note incorrectly claimed it did. `PB4`/`PB5` are
already committed there too — Green LED and Red LED respectively
(`../receiver/docs/wiring.md`) — so the alternate mapping just trades one
conflict (solder bridges) for a different one (existing LED wiring) on that
specific board. That board's issue is genuinely zero free pins, not a
solder-bridge issue, and is already tracked separately below (Nucleo-64
fallback). The SPI mapping choice here only matters for the 3 fresh,
otherwise-unwired boards used for initial bring-up.

## Open design decision: adapt an existing STM32 fork, or write a minimal custom driver

Two community STM32 ports of TMRh20's RF24 library exist:
[TheDIYGuy999/RF24_STM32](https://github.com/TheDIYGuy999/RF24_STM32) and
[MarkKharkov/RF24](https://github.com/MarkKharkov/RF24). At least one report
([nRF24/RF24#501](https://github.com/nRF24/RF24/issues/501)) got basic SPI
communication working but had unreliable inter-node communication — so
adapting one of these isn't a guaranteed shortcut, just a possible head start.

Alternative: a minimal driver written directly against the nRF24L01+
datasheet's register map. Our protocol only needs a small slice of what the
full library supports — fixed 5-byte writes (handle), poll-based
`available()`/`read()` (receiver, no interrupt-driven mode needed), no ack,
no dynamic payload, no multicast. That's plausibly *less* total code than
untangling someone else's partial port, and easier to safety-review since
it would only contain what's actually used.

**Decided, 2026-08-06: minimal custom driver.** Implemented as
`DEVELOPMENT/radio/spi-bringup/spi-bringup/{Inc,Src}/nrf24.c/.h` — a
register-level driver against the nRF24L01+ (Plus) datasheet covering only
what this project's link needs (single/multi-byte register read/write,
init with no-ack/no-retry/250kbps+PA_HIGH/fixed 5-byte payload). Chosen over
adapting TheDIYGuy999/RF24_STM32 or MarkKharkov/RF24 per the reasoning
above and ADR 0006 (pure STM32 HAL, no Arduino/STM32duino).

## Known integration constraint for later: pin budget

The receiver bench rig already committed **all 17 free GPIOs** on the
L432KC's Arduino-style header — "no margin" per `../receiver/docs/wiring.md`.
SPI (SCK/MOSI/MISO/CSN) + CE needs 4 more pins than that board has free
(IRQ isn't needed since we poll, matching the commented-out
`radio.available()` calls already in both firmwares). This doesn't block
the initial bring-up here (happening on a fresh, otherwise-unused board),
but merging radio into the actual populated receiver rig later will need
either freeing pins (e.g. dropping the 7-segment readout or heartbeat LED)
or bumping to a Nucleo-64 — already noted as the fallback in
`docs/OPEN-ITEMS.md`.

## Suggested bring-up order (once hardware arrives)

1. **Single board + single module**: confirm SPI transactions work at all —
   read back a known register (e.g. `CONFIG`, datasheet default `0x08`) and
   confirm it matches. Logic analyzer on SCK/MOSI/MISO/CSN/CE to sanity-check
   the transaction shape against the datasheet timing diagrams.

   **Done, 2026-08-06.** `DEVELOPMENT/radio/spi-bringup/` (stock/unmodified
   Nucleo, SPI1 on PB3/PB4/PB5) read back `CONFIG` (addr `0x00`) as `0x08`
   and `STATUS` as `0x0E` — both exactly the nRF24L01+ datasheet's
   power-on-reset defaults for a fresh module. Confirmed via CubeIDE
   debugger (breakpoint after the transaction, `rx_buf[0]`/`rx_buf[1]`
   inspected as Expressions), not yet cross-checked against the logic
   analyzer's captured waveform — worth doing at some point for the
   SPI-mode/timing sanity check, but not blocking since the chip is already
   responding with correct data.

   **Multi-byte register read/write also confirmed, 2026-08-06.** The
   `nrf24.c` driver's `nrf24_write_reg_n`/`nrf24_read_reg_n` (used for the
   5-byte `RX_ADDR_P0`/`TX_ADDR` address registers) round-tripped correctly
   on hardware: after `nrf24_init()`, `CONFIG` read back `0x0A`, `RF_SETUP`
   read back `0x24`, and `RX_ADDR_P0` read back `{0xE7,0xE7,0xE7,0xE7,0xE7}`
   — all matching the values the driver itself wrote, confirming both the
   single- and multi-byte read and write paths. (An early single-call-per-byte
   implementation of the `_n` functions read back all-zero instead; fixed by
   building one cmd+data buffer and sending it in one `HAL_SPI_TransmitReceive`
   call, matching the single-register functions' already-proven pattern.)

2. ~~Decide fork-vs-custom-driver~~ — done, see above.
3. **Two boards + two modules**: a raw TX/RX smoke test independent of
   `handle_firmware.c`/`receiver_firmware.c` — e.g. one side sends an
   incrementing counter, the other prints/blinks it. Proves the link itself
   before touching either firmware.

   **Done, 2026-08-06.** Board A (PTX role) sends an incrementing counter
   every ~300ms; board B (PRX role) receives it and stores the value for
   debugger inspection (`rx_last_counter`), confirmed climbing by 1 each
   time, matching board A. Two real bugs found and fixed along the way —
   see `docs/decisions/0005-radio-choice-and-ignition-emi.md`'s 2026-08-06
   addendum for the full detail, since both are likely clone-chip symptoms
   relevant to eventual production hardware sourcing:
   - `nrf24_send_payload()` now clears `STATUS` and flushes the TX FIFO
     after every send (not just once at init) — without this, the TX
     board's FIFO latched permanently full (`MAX_RT` stuck set) within a
     few send cycles, despite `EN_AA`/`SETUP_RETR` both confirmed `0x00`.
   - `RF_SETUP` is temporarily `1Mbps` instead of the originally-intended
     `250kbps` — at 250kbps, zero packets were ever received even with the
     above fix and both boards side by side with antennas seated; switching
     to 1Mbps with no other change made reception immediately reliable.
     **This is a bring-up diagnostic swap, not a data-rate decision** — see
     the ADR 0005 addendum before assuming it holds for other hardware.

4. **Only after that**: wire the real `radio.*` calls into
   `handle_firmware.c` and `receiver_firmware.c` (currently fully
   commented-out stubs) and re-verify both bench rigs end-to-end over real
   radio instead of GPIO-simulated input.

   **Driver promoted, 2026-08-07; firmware wiring still blocked.** The
   proven `nrf24.c`/`nrf24.h` driver now lives in `src/common/` (shared,
   real, compiled code - not the bring-up project's copy), parameterized
   via an `nrf24_handle_t` so handle and receiver each supply their own SPI
   peripheral and CE/CSN pins rather than relying on hardcoded globals. Both
   firmware files' stub comments were updated to reference the real
   `nrf24_*` API (replacing stale TMRh20/RF24-library-style pseudocode from
   before the fork-vs-custom-driver decision), but the calls themselves stay
   commented, not live, because two concrete prerequisites are still open:
   - **Receiver**: no free GPIOs for SPI+CE — the bench rig already
     committed all 17 (see "Known integration constraint" above). Needs
     either freeing pins or a Nucleo-64 before real pins can be assigned.
   - **Handle**: no real hardware project exists yet at all (unlike the
     receiver's `DEVELOPMENT/receiver/firmware/`) — only the bring-up boards
     have been wired/programmed so far.

   Host compile-checks (`test/test_logic.c` and both firmware
   `.c` files) still pass unchanged - `nrf24.h`/`nrf24.c` depend on the real
   STM32 HAL and are new, not part of either check.

## Related docs

- `docs/decisions/0005-radio-choice-and-ignition-emi.md` — why
  nRF24L01+PA+LNA specifically, and the EMI environment to design around
  once this is mounted near a running engine.
- `docs/decisions/0001-no-radio-ack.md` (see the 2026-08-02 addendum) — no
  radio-level ack on the control uplink; a telemetry downlink is deferred
  and out of scope here.
- `docs/OPEN-ITEMS.md` — "RF24 HAL port" (Firmware TODOs) and "RF range
  test" (Hardware TODOs) entries.
- `docs/PROJECT_DESIGN.md` — toolchain list (logic analyzer already listed
  there).
