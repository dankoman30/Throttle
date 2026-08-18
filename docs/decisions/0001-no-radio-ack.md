# 0001 — No radio-level acknowledgement

**Status:** accepted · **Date:** 2026

## Decision

Run the nRF24L01+ link with `setAutoAck(false)`: the handle transmits fixed-rate
(80 Hz), one-way, and the receiver never acks at the radio layer.

## Why

Delivery is instead covered by three cheaper mechanisms working together:
fixed-rate repetition (a lost packet is superseded ~12.5 ms later), the packet
**sequence number** (stale/duplicate/reordered packets are discarded), and the
receiver **loss-of-signal watchdog** (ramps to idle when packets stop). An ack
round-trip would add latency and complexity without materially improving safety,
because the watchdog already handles "packets stopped arriving."

Kill is made robust to loss another way: it is **latched on the handle** and
resent every packet, so a brief press produces a sustained stream (see
`docs/decisions/0004-cruise-on-handle.md` neighbor rationale and
`src/handle/handle_firmware.c`).

## Addendum (2026-08-02): one-way vs. two-way link reconsidered, and resolved

Revisited whether to make the link bidirectional — both for confirmed delivery
on the control uplink (this ADR's original open question) and, separately, for
a receiver→handle telemetry downlink (receiver state, current throttle
position — neither of which currently reach the pilot; battery level is no
longer a candidate, see the 2026-08-17 addendum below).

**These are two separate questions, not one "go 2-way" decision:**
confirmed-delivery control and best-effort telemetry have very different risk
profiles, and treating them as a single bidirectional-vs-not choice was the
wrong frame.

**Control uplink: stays one-way and unacked. Decision made — not revisiting
without new bench evidence.**
- Kill's original open question is answered **no**. Kill is already latched +
  resent every packet, so a brief press already survives arbitrary packet
  loss on its own; ACK only helps in the fully-dead-link case, and if the
  link is fully dead there's no ACK coming back either — that case is already
  covered by the independent mechanical kill line, with zero radio and zero
  power required. ACK doesn't close a gap that isn't already closed two other
  ways.
- `radio.write()` with auto-ack enabled **blocks until it gets an ACK back or
  exhausts its retries**. Under the ignition-EMI condition ADR 0005 already
  names as the dominant reliability threat, that's exactly when ACKs are
  least likely to come back cleanly — so auto-ack would mean *more* blocking
  and retrying precisely when the link is already stressed. Today a lost
  packet costs nothing (the next one supersedes it in ~12.5ms regardless);
  with auto-ack, a lost packet can stall the sender. That's a regression on
  the exact axis this ADR already optimized for, in the one file (the
  handle's send path feeding into the receiver's safety state machine) that
  should change the least.

**Telemetry downlink: worth pursuing, but as a separate, deferred,
structurally-isolated feature — not part of first radio bring-up.**
- The gap is real: receiver state currently has no path to the pilot at
  all - it's only ever visible on the receiver's own status LED, mounted
  next to the engine, not somewhere a pilot in flight can reliably see.
- If/when built: strictly one-way (receiver → handle), a separate packet
  type/pipe from control, and architected so the handle's 80Hz control-send
  loop stays authoritative — it only opportunistically listens for telemetry
  in slack time, never at the cost of a control packet going out on schedule.
  Telemetry loss or staleness must be purely cosmetic on the handle's
  display; it must be structurally impossible for a telemetry hiccup to
  delay, corrupt, or gate a throttle/kill/start command. `receiver_firmware.c`
  has no concept of a return link today, and should stay that way regardless
  of what the handle later does with telemetry — telemetry must be layered
  on top, never routed through the existing control/state-machine code path.
- Also has a real handle-side battery-life cost (periodic RX duty cycle vs.
  today's send-only handle radio) worth sizing before committing.
- Deferred until after the primary control link (RF24 HAL port, both bench
  rigs radio-verified) is working. Tracked in `docs/OPEN-ITEMS.md`.

### Addendum (2026-08-08): what the handle should show once telemetry exists

Pilot requirement, recorded ahead of implementation: the handle's own display
should show ~~**both packs' battery level side by side** (its own, read
locally as today; the receiver's, via telemetry once built)~~ plus the
**receiver's system status** (cranking / idle-armed / killed, mirroring
`receiver_firmware.c`'s `throttle_state_t`). All of this is receive-side
UI work on the handle - doesn't change anything about the telemetry
transport constraints above (still one-way, still cosmetic-only, still
never gating a control packet).

**Battery-level half superseded (2026-08-17): no battery indication of any
kind, on either board, ever - not local, not telemetered.** All battery
sensing was removed project-wide (see `docs/OPEN-ITEMS.md` "Battery readout
wiring"); standalone meters on the packs are the only battery indication
either unit has, by decision. If telemetry is ever built, it carries
**receiver system status only** - the battery-level half of this pilot
requirement no longer applies.
