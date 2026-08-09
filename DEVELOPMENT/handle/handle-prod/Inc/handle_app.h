/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Daniel Koman */
#ifndef HANDLE_APP_H
#define HANDLE_APP_H

/* Tri-color status LED behavior. All four states are resolved entirely from
 * state the handle already knows locally (no telemetry downlink exists -
 * see the 2026-08-08 addendum in docs/decisions/0001-no-radio-ack.md - so
 * "no link" and a confirmed "running" receiver state are NOT shown here,
 * only what this board itself knows or is actively commanding):
 *   Red (blinking) - g_kill_latched. Kill is sticky, so once lit this only
 *                     clears on a physical re-arm (power cycle), same as
 *                     the receiver's own kill indication.
 *   Blue  (solid)  - g_cruise_engaged.
 *   Yellow (solid) - g_start_hold_confirmed: a proxy for "cranking" (the
 *                     handle is actively transmitting CMD_FLAG_START_REQ),
 *                     not confirmation the receiver is actually cranking.
 *                     Red+green together, same color-mixing trick as any
 *                     other 3-channel LED.
 *   Green (solid)  - default/idle: not killed, not cruising, not holding
 *                     start.
 * Priority top-to-bottom: kill always wins (mirrors build_packet()'s own
 * kill-suppresses-cruise-and-start ordering), then cruise, then start-hold.
 */

/* Call once from main()'s USER CODE BEGIN 2, after all MX_*_Init() calls. */
void handle_app_init(void);

/* Call every iteration of main()'s while(1), in USER CODE BEGIN 3. Paces
 * its own sub-tick internally (packet build + send gated to
 * HANDLE_TX_PERIOD_MS) - safe to call as fast as the loop spins. */
void handle_app_tick(void);

#endif /* HANDLE_APP_H */
