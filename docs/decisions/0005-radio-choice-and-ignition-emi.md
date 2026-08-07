# 0005 — Radio choice (nRF24L01+PA+LNA, 2.4 GHz) and ignition EMI

**Status:** accepted (radio choice) · open (EMI characterization) · **Date:** 2026

## Decision

Use the **nRF24L01+ with PA+LNA** on the 2.4 GHz ISM band for the handle→receiver
link, bought from a reputable supplier (Mouser/DigiKey), not clone boards.

## Why this module / band

- Cheap, low-latency, mature library support (RF24/TMRh20), easy to fit a small
  fixed-packet protocol on top of — a good match for 5-byte packets at 80 Hz.
- **PA+LNA over the bare module** for range margin: the RF path is not clean air
  — it runs through the pilot's body and the frame/cage. Bare-module real-world
  range in clutter is poor; PA+LNA buys margin so the loss-of-signal watchdog
  stays a rare fallback, not a routine mode.
- Power draw is not the constraint (both ends are battery/engine powered, not
  coin-cell), so the low-power appeal of the Nordic line is a bonus, not the
  driver.

**Clone caveat:** the clone market for this exact module is full of mismatched
PA/LNA matching networks, wrong crystals, and mislabeled bare modules — which
would silently erase the margin PA+LNA is bought for. Worse than a known-limits
bare module for a safety-critical part.

## Why not sub-GHz (433/915) instead

Sub-GHz would help with body/frame **penetration** and WiFi **congestion**, and
is a legitimate contender if range testing shows 2.4 GHz can't make margin. But
it is **not** a fix for ignition EMI (see below) — that noise is broadband and
hits sub-GHz too. Band choice and EMI are separate problems with separate fixes.

## Ignition EMI — the dominant reliability threat

Two-stroke CDI ignition produces nanosecond-edge, high-voltage switching events:
a broadband noise source whose harmonics reach into 2.4 GHz (the same reason gas
RC models historically wrecked receivers). It is **RPM-dependent** because spark
rate — and thus harmonic spacing — scales with RPM, so specific RPM bands can
land noise on the operating channel. Our **receiver sits right next to the
engine**, the worst position relative to the source.

### How this interacts with the firmware (the key insight)

- EMI-corrupted packets **fail sync/CRC/seq and are discarded** — they cannot
  produce false throttle, start, or kill. The failure mode is therefore
  **dropout**, indistinguishable from loss of signal.
- So sustained EMI in an RPM band makes the **watchdog ramp throttle toward
  idle** — the safe direction, but a real flyability issue. Engine EMI is what
  will exercise the watchdog most.
- Margin already present: 80 Hz ⇒ 12.5 ms/packet; watchdog threshold A is
  175 ms ≈ **14 consecutive lost packets** before any ramp. Sporadic corruption
  is absorbed silently; only sustained corruption shows up as throttle sag.
- **Kill line is the sharp edge:** fail-safe NC kill (open = kill) run near the
  engine can be pulled toward "kill" by coupled EMI. `KILL_DEBOUNCE_MS` (30 ms)
  rejects short spikes, but sustained EMI could nuisance-latch a shutdown →
  ferrites + twisted/shielded kill wiring + routing away from the CDI are
  required, not optional.

### Mitigations (at the source first)

1. Resistor spark-plug cap / resistor plug — damp spark edge rate at the source.
2. Shielded plug lead, grounded one end.
3. Physical separation — receiver + antenna as far from coil/CDI/plug lead as
   the frame allows (EMI falls off fast with distance).
4. Antenna routing away from the CDI box; short leads; consider a small ground
   plane/shield between antenna and engine.
5. Ferrite chokes on kill-switch wiring and power leads near the ignition.
6. Good chassis/CDI/coil grounding (low-impedance return beats radiation).
7. Supply decoupling on the receiver STM32 rail (noise couples via power, not
   just the antenna).

## Addendum, 2026-08-06 — clone symptoms observed on bring-up hardware

The bring-up modules (4x nRF24L01+PA+LNA, bought from Addicore, not
Mouser/DigiKey as this ADR specifies) showed behavior consistent with the
clone caveat above during `DEVELOPMENT/radio/` two-board TX/RX bring-up:

- The TX side latched a `MAX_RT` (max retransmits) fault on nearly every
  send despite `EN_AA=0x00` and `SETUP_RETR=0x00` both confirmed via
  register read-back — which should make `MAX_RT` unreachable on genuine
  nRF24L01+ silicon. This matches a documented issue with the SI24R1 clone
  (the chip most commonly found mislabeled as genuine on this module
  category): inverted ACK-related bit behavior relative to the real part.
  Worked around in the bring-up driver by clearing `STATUS` and flushing
  the TX FIFO after every send, not just once at init.
- `250kbps` produced zero successful receptions at close range even after
  the above fix (TX confirmed completing sends via `STATUS.TX_DS`); simply
  switching to `1Mbps` with no other change made reception work reliably.
  Community reports describe 250kbps specifically as unreliable on various
  nRF24L01/clone modules, consistent with this.

Neither symptom proves these specific units are SI24R1 clones (no chip-ID
readback was done), but both are exactly the failure signature the clone
caveat above warned about. **Relevant for the eventual production sourcing
decision**: if final units are bought from Mouser/DigiKey per this ADR's
original guidance, re-run this same bring-up test (register read-back on
`EN_AA`/`SETUP_RETR` matching intent, and a real reception test at 250kbps)
before assuming that hardware behaves the same way.

## Consequences / follow-ups (tracked in OPEN-ITEMS.md)

- Two distinct bench tests: (a) handle→receiver range through real body/frame;
  (b) **engine RPM sweep** logging consecutive-packet-loss vs RPM with the
  receiver mounted in its real location.
- Possible channel selection to dodge both WiFi and the engine's worst harmonic
  bands; scan on boot.
- Low-priority: evaluate CRC8 → CRC16 given the hostile environment (packet
  5→6 bytes). CRC8 is defensible today thanks to sync + seq filtering and
  throttle rate-limiting containing any single false-accept.
