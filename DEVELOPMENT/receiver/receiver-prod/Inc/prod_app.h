/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Daniel Koman */
#ifndef PROD_APP_H
#define PROD_APP_H

/* Status LED (red+green only - the tri-color package's blue lead was
 * dropped 2026-08-09 in favor of a separate, dedicated cruise LED; see
 * "Cruise indicator" below). Priority order, highest first:
 *   1. STATE_KILLED       - red, LONG-LONG blink (src/common/led_blink.h's
 *                            KILL_PATTERN).
 *   2. STATE_STARTING     - yellow (red+green together), solid.
 *   3. Running proxy      - green, solid. Latches on a VOLUNTARY release
 *                            from STATE_STARTING (never a forced stop -
 *                            loss-of-signal abort or the MAX_CRANK_MS
 *                            backstop), persists until the next STARTING
 *                            or KILLED transition. Same rule as the bench
 *                            rig's separate green LED used to be.
 *   4. Default (armed,    - red, LUB-DUB heartbeat (HEARTBEAT_PATTERN).
 *      never yet started)   This is what shows right after boot/re-arm,
 *                            before any crank has ever completed - not
 *                            green.
 * There is deliberately no battery indication anywhere on this board - no
 * battery sense pin, no LED behavior tied to pack voltage. Standalone
 * battery meters on the packs themselves cover that instead (see
 * docs/OPEN-ITEMS.md).
 *
 * Cruise indicator (separate, dedicated LED, physically relocated off the
 * old tri-color package's blue lead onto its own standalone LED on the
 * same pin, now labeled CRUISE_LED): solid while g_cruise_active
 * (receiver_firmware.c), off otherwise. Independent of the state model
 * above entirely - see DEVELOPMENT/handle/handle-prod/Inc/handle_app.h for
 * the handle's mirror of this exact model, both status and cruise.
 */

/* Call once from main()'s USER CODE BEGIN 2, after all MX_*_Init() calls. */
void prod_app_init(void);

/* Call every iteration of main()'s while(1), in USER CODE BEGIN 3. Paces
 * its own sub-ticks internally (radio polled every call, control logic
 * every ms) - safe to call as fast as the loop spins. */
void prod_app_tick(void);

#endif /* PROD_APP_H */
