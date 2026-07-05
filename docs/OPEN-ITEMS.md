# Open Items — action items & unresolved questions

Living tracker for this project. Add items as they come up; check them off when
resolved (and record *why* in `docs/decisions/` if it's a design decision worth
keeping). Grouped by area, most safety-relevant first.

Legend: `[ ]` open · `[x]` done · `[~]` in progress / partially done.

## Safety-critical / blocks flight-readiness

- [ ] **Engine-caught detection (`STARTING` → `RUNNING`).** Decide the signal
  (RPM sense / vibration switch / timed window) and implement the transition in
  `src/receiver/receiver_firmware.c` (`handle_valid_packet`, marked as a
  follow-up in the code). Until done, the project is **not flight-ready**.
- [ ] **Bench-verify fail-safe kill.** Confirm on real hardware that a
  disconnected/broken kill wire reads as KILL (normally-closed + pull-up, open =
  HIGH = kill). Verify latch + `KILL_DEBOUNCE_MS` rejects vibration glitches but
  latches a genuine open. See `docs/decisions/0003-fail-safe-kill-polarity.md`.
- [ ] **Tune loss-of-signal watchdog** on the bench: `WATCHDOG_RAMP_START_MS`
  (175), `WATCHDOG_FULL_IDLE_MS` (600), `RAMP_TO_IDLE_DURATION_MS` (400),
  `LINK_RESTORE_STABLE_MS` (300). Current values are starting points.
- [ ] **Aux-output policy during KILL / loss of signal.** Confirm desired
  behavior: smoke off when killed and on full signal loss (safety), lights
  independent. Currently `apply_aux_outputs` mirrors flags after the safety
  state machine; decide whether smoke must be force-gated in firmware.
- [ ] **Confirm mechanical kill wiring** is independent of the MCU and grounds
  the ignition line with zero power to electronics (backup for a dead radio).

## Hardware decisions

- [ ] **Servo selection** — measure actual throttle-cable pull force/travel with
  a fish scale across the full stroke before ordering (~15–25 kg·cm digital
  metal-gear, continuous-duty ballpark).
- [ ] **RF range test** — measure real handle↔receiver distance/reliability
  through frame/cage/body to confirm nRF24L01+PA+LNA gives enough margin.
- [ ] **Battery profiles + divider ratios** — replace placeholder mV values in
  `HANDLE_BATT` / `RX_BATT` and the `read_battery_mv()` scaling with measured
  values for the actual packs on each side.
- [ ] **Kill relay + starter relay drive circuits** — design; add a bounded
  starter pulse duration + cooldown (noted in `fire_starter`).
- [ ] **Connectors** — locking, vibration-rated (JST-SM minimum;
  Deutsch/Amphenol preferred).
- [ ] **Battery readout wiring** — 3/4-LED bar + piezo buzzer per side.
- [ ] **Cruise / accessory switch wiring** — momentary (cruise) + rocker/momentary
  (lights, smoke), all "closed = on" with pull-downs (kill is the exception).

## Firmware TODOs

- [ ] Wire HAL peripheral inits (ADC, GPIO, TIM PWM) marked `/* fill in */`.
- [ ] RF24 HAL port (TMRh20 is Arduino-first) for both ends.
- [ ] Servo PWM mapping: 0–255 throttle → pulse-width for the chosen servo.
- [ ] Generate the STM32CubeIDE projects (none committed yet).
- [ ] Open question: does **kill** warrant a confirmed-delivery/ack path, versus
  the current fixed-rate send + latch? See `docs/decisions/0001-no-radio-ack.md`.

## Licensing / legal

- [x] Firmware licensed GPL-3.0-or-later (`LICENSE`, SPDX headers).
- [ ] Add hardware `LICENSE` (CERN-OHL-S) once schematic/PCB files exist.
- [x] Loud safety disclaimer + no-liability-for-forks note in `README.md`.
- [ ] Optional: brief consult with a lawyer familiar with open safety-critical
  hardware before wide distribution / before anyone flies it.

## Docs / process

- [x] Seed architecture decision records in `docs/decisions/`.
- [ ] Keep `src/common/` byte-identical assumption enforced (see the
  `protocol-guardian` subagent in `.claude/agents/`).
