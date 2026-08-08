/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Daniel Koman */
#ifndef PROD_APP_H
#define PROD_APP_H

/* Tri-color status LED behavior:
 *   Red   - heartbeat while armed (toggles every ~200ms, or ~100ms if the
 *           receiver's own battery is low - see g_batt_low); solid ON while
 *           STATE_KILLED, overriding the blink entirely.
 *   Green - engine-running proxy, lit only on a VOLUNTARY release from
 *           STATE_STARTING (never a forced stop - loss-of-signal abort or
 *           the MAX_CRANK_MS backstop), off during STARTING/KILLED. Same
 *           rule as the bench rig's separate green LED.
 *   Blue  - on while STATE_STARTING (cranking), off otherwise.
 * Kill and battery-low both wanting the red channel is intentional: kill is
 * the higher-priority signal, so battery status isn't shown while killed.
 */

/* Call once from main()'s USER CODE BEGIN 2, after all MX_*_Init() calls. */
void prod_app_init(void);

/* Call every iteration of main()'s while(1), in USER CODE BEGIN 3. Paces
 * its own sub-ticks internally (radio polled every call, control logic
 * every ms) - safe to call as fast as the loop spins. */
void prod_app_tick(void);

#endif /* PROD_APP_H */
