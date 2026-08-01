/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Daniel Koman */
#ifndef BENCH_APP_H
#define BENCH_APP_H

/* Set from the polarity test in DEVELOPMENT/receiver/docs/wiring.md:
 *   1 = common anode   (common pin -> 3V3, segments driven LOW to light)
 *   0 = common cathode (common pin -> GND, segments driven HIGH to light)
 * Unconfirmed for the 5611AH on hand - verify before trusting the display. */
#define DISPLAY_COMMON_ANODE   0

/* Call once from main()'s USER CODE BEGIN 2, after all MX_*_Init() calls. */
void bench_app_init(void);

/* Call every iteration of main()'s while(1), in USER CODE BEGIN 3. Paces
 * its own sub-ticks internally (ingestion at HANDLE_TX_RATE_HZ, control
 * logic every ms) - safe to call as fast as the loop spins. */
void bench_app_tick(void);

#endif /* BENCH_APP_H */
