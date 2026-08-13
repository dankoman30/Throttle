/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Daniel Koman */
#ifndef HANDLE_APP_H
#define HANDLE_APP_H

/* Status LED (red+green only - the tri-color package's blue lead was
 * dropped 2026-08-09 in favor of a separate, dedicated cruise LED; see
 * "Cruise indicator" below). All states are resolved entirely from state
 * the handle already knows locally (no telemetry downlink exists - see the
 * 2026-08-08 addendum in docs/decisions/0001-no-radio-ack.md - so "no
 * link" and a confirmed "running" receiver state are NOT shown here, only
 * what this board itself knows or is actively commanding). Priority order,
 * highest first, mirroring the receiver's own priority table
 * (DEVELOPMENT/receiver/receiver-prod/Inc/prod_app.h) as closely as this
 * board's local-only information allows:
 *   1. g_kill_latched     - red, LONG-LONG blink (src/common/led_blink.h's
 *                            KILL_PATTERN). Kill is sticky, so once this
 *                            starts it only clears on a physical re-arm
 *                            (power cycle), same as the receiver's own
 *                            kill indication. Mirrors build_packet()'s own
 *                            kill-suppresses-cruise-and-start ordering.
 *   2. g_start_hold_confirmed - yellow (red+green together), solid. A
 *                            proxy for "cranking" (the handle is actively
 *                            transmitting CMD_FLAG_START_REQ), not
 *                            confirmation the receiver is actually
 *                            cranking.
 *   3. Running proxy       - green, solid. Latches on release of a
 *                            confirmed start-hold (the handle's only local
 *                            proxy for "presumed running" - no telemetry
 *                            confirms the real thing); a fresh start-hold
 *                            or a kill both clear it.
 *   4. Default (armed,     - red, LUB-DUB heartbeat (HEARTBEAT_PATTERN).
 *      never yet started)    This is what shows right after boot or a
 *                            re-arm, before any start-hold has ever been
 *                            confirmed and released - not green.
 *
 * Cruise indicator (separate, dedicated LED, physically relocated off the
 * old tri-color package's blue lead onto its own standalone LED on the
 * same pin, now labeled CRUISE_LED): solid while g_cruise_engaged, off
 * otherwise. Independent of the status LED's state model above entirely.
 */

/* Call once from main()'s USER CODE BEGIN 2, after all MX_*_Init() calls. */
void handle_app_init(void);

/* Call every iteration of main()'s while(1), in USER CODE BEGIN 3. Paces
 * its own sub-tick internally (packet build + send gated to
 * HANDLE_TX_PERIOD_MS) - safe to call as fast as the loop spins. */
void handle_app_tick(void);

#endif /* HANDLE_APP_H */
