# DEVELOPMENT/radio — RF24 HAL port & radio link bring-up

**Status: hardware being ordered, nothing built yet.** This doc exists so
picking this up when the parts arrive doesn't require re-deriving context
from scratch.

## What this covers

Bringing up the actual nRF24L01+ link between `handle` and `receiver`. Both
`src/handle/handle_firmware.c` and `src/receiver/receiver_firmware.c` currently
have the entire radio path commented out (`radio.begin()`, `radio.write()`,
`radio.available()`/`radio.read()`, etc.) — the packet/state-machine logic on
both ends is already proven (`DEVELOPMENT/receiver/`), but nothing has actually
gone over the air yet. `docs/OPEN-ITEMS.md` flags the RF24 HAL port as staying
"on the critical path" for exactly this reason.

## On order

1. **3x nRF24L01+PA+LNA modules** (2 for the link + 1 spare). Clone chips
   (silkscreened as genuine Nordic but actually `SI24R1`/Beken parts) are a
   known problem for this exact module category and can't be reliably told
   apart by inspection (Nordic is fabless; markings vary by fab run) — the
   practical mitigation is buying multiple units from one reputable, consistent
   source rather than mixing cheap unverified listings, per ADR 0005's "not
   clone boards" requirement. Considered reliable for this assembled module:
   [Addicore](https://www.addicore.com/products/nrf24l01-pa-lna-with-antenna-2-4ghz-wireless-transceiver)
   (~$6/module), [ProtoSupplies](https://protosupplies.com/product/nrf24l01palna-2-4ghz-rf-wireless-module/),
   [HandsOn Tech](https://handsontec.com/index.php/product/nrf24l01palna-2-4ghz-rf-transceiver-module/).
   Get the version with a **fixed/soldered antenna** (or confirm one's
   included if SMA) — some boards ship bare with no antenna.
2. **A third STM32L432KC Nucleo-32.** The existing two are both tied up in
   the fully-wired, SB16/SB18-modified, already-verified receiver bench rig
   (`../receiver/`) — do the initial radio bring-up on a fresh board instead
   of disturbing that one.
3. **Decoupling capacitors**: 10µF+ (electrolytic/tantalum) plus a 0.1µF
   ceramic across each module's V+/GND pins, placed as close to the module as
   physically possible. **Known failure mode without this**: the PA+LNA
   variant draws real current spikes on TX (order ~100+mA) that a Nucleo's
   small onboard 3.3V LDO isn't sized for — this is one of the most common
   causes of "the radio just doesn't work / works intermittently" reports for
   this module. If it's still flaky with local decoupling, move to a
   dedicated 3.3V supply rather than the Nucleo's onboard rail.
4. **A cheap 8-channel Saleae-compatible logic analyzer** (~$10–25, e.g. a
   24MHz DFRobot-class clone). Already on the toolchain wishlist in
   `docs/PROJECT_DESIGN.md`; SPI bring-up (SCK/MOSI/MISO/CSN/CE — 5 signals,
   comfortably within 8 channels) is exactly the task it's for, and would
   have shortened the SB16/SB18 debugging saga considerably had we had it
   for register-level visibility instead of just GPIO reads.

No level shifting needed either way — the module's SPI inputs are 5V-tolerant
and its onboard regulator accepts 1.9–3.6V, so it's a direct match for the
L432KC's 3.3V logic.

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

**Decide this once modules arrive and basic SPI register read-back is
confirmed working** (see bring-up order below) — the choice doesn't change
anything about the hardware or wiring either way.

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
2. Decide fork-vs-custom-driver (above) once SPI is proven solid.
3. **Two boards + two modules**: a raw TX/RX smoke test independent of
   `handle_firmware.c`/`receiver_firmware.c` — e.g. one side sends an
   incrementing counter, the other prints/blinks it. Proves the link itself
   before touching either firmware.
4. **Only after that**: wire the real `radio.*` calls into
   `handle_firmware.c` and `receiver_firmware.c` (currently fully
   commented-out stubs) and re-verify both bench rigs end-to-end over real
   radio instead of GPIO-simulated input.

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
