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

## Open question

Whether **kill specifically** should get a confirmed-delivery path is still open
(tracked in `docs/OPEN-ITEMS.md`). The mechanical kill line is the ultimate
backup regardless.
