/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Daniel Koman */
#ifndef UNIT_CONFIG_H
#define UNIT_CONFIG_H

/* ---------------------------------------------------------------------
 * Per-unit nRF24 address - see docs/OPEN-ITEMS.md "Multi-unit isolation"
 * for why this exists and docs/decisions/0009-per-unit-address-compile-
 * time.md for why it's a compile-time constant here rather than a
 * hardware DIP/rotary switch.
 *
 * TRACKED IN GIT with UNIT_NUMBER = 0xFF below - do NOT commit a real
 * per-unit value back to this file. Building a real production unit means
 * editing UNIT_NUMBER locally, flashing, and leaving that edit uncommitted
 * (or reverting it before committing anything else on this file).
 *
 * Set UNIT_NUMBER to:
 *   - This pair's assigned number (see docs/UNIT-REGISTRY.md) for a real
 *     production unit. Valid range: 0x01-0xFE.
 *   - 0xFF (the default here) for a deliberate unpaired bench/test build -
 *     a reserved value, never assigned to a real pair.
 *   - 0x00 is the reserved "still unassigned" marker and the #error below
 *     refuses to even build with it set - if you see that error, you
 *     forgot to set this.
 * ------------------------------------------------------------------- */
#define UNIT_NUMBER 0xFFu

#if UNIT_NUMBER == 0x00u
#error "UNIT_NUMBER is still the unassigned placeholder (0x00) - set it to \
this unit's real assigned number (see docs/UNIT-REGISTRY.md), or leave it \
at 0xFF for a deliberate unpaired bench/test build."
#endif

/* True for a deliberate unpaired bench/test build - handle_app.c and
 * prod_app.c invert their status LED's heartbeat pattern when this is
 * set, as a visual "this is not a real paired unit" cue on real hardware
 * (see docs/decisions/0009-per-unit-address-compile-time.md). */
#define UNIT_IS_TEST_BUILD (UNIT_NUMBER == 0xFFu)

/* Fixed prefix shared by every real unit's address (see
 * docs/UNIT-REGISTRY.md) - combine with UNIT_NUMBER to build the full
 * 5-byte nRF24 address:
 *   static const uint8_t addr[5] = { UNIT_NRF24_ADDR_PREFIX, UNIT_NUMBER };
 */
#define UNIT_NRF24_ADDR_PREFIX 0xE7u, 0xE7u, 0xE7u, 0xE7u

#endif /* UNIT_CONFIG_H */
