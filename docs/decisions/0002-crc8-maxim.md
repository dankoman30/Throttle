# 0002 — CRC-8/MAXIM for packet integrity

**Status:** accepted · **Date:** 2026

## Decision

Protect each packet with **CRC-8/MAXIM** (Dallas 1-Wire polynomial, `0x31`
reflected = `0x8C`), computed over the first 4 bytes, independent of the radio's
own CRC. Implemented bit-banged in `src/common/crc8.h`.

## Why

- Small and cheap on an STM32 (bit-banged cost is negligible at 80 Hz).
- Well-known, easy to **hand-verify against a published test vector**:
  `crc8_compute("123456789", 9) == 0xA1`. This is asserted in
  `test/test_logic.c`.
- Independent of the radio-layer CRC, so corruption introduced anywhere in the
  handle→receiver path (not just over the air) is caught before the packet is
  acted on.

Chosen over a rolling custom checksum specifically because a standard,
test-vectored algorithm is auditable and hard to get subtly wrong.
