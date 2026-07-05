# Open Items — action items & unresolved questions

Living tracker for this project. Add items as they come up; check them off when
resolved (and record *why* in `docs/decisions/` if it's a design decision worth
keeping). Grouped by area, most safety-relevant first.

Legend: `[ ]` open · `[x]` done · `[~]` in progress / partially done.

## Safety-critical / blocks flight-readiness

- [x] **Engine start / "caught" detection — resolved: MANUAL crank** (ADR 0007).
  Pilot holds the start button to crank and releases when the engine catches
  (they hear it + have a separate RPM gauge); no tach in the controller, no
  RPM-based `STARTING → RUNNING`. Crank is bounded by a loss-of-signal abort and
  a max-crank cap. This removed the tach subsystem and two non-fail-safe guards.
  (The project is still **not flight-ready** for other reasons — HAL bring-up,
  servo selection, hardware — see below.)
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
- [x] ~~`RUNNING` → `IDLE_SAFE` restart policy~~ / ~~RPM START guard not fail-safe~~ —
  both **moot** under manual crank (ADR 0007): `STATE_RUNNING` no longer exists
  and the tach-dependent `rpm == 0` START guard is gone. The pilot can always
  re-crank (rising-edge + cooldown); protection against cranking an already-running
  engine now relies on pilot awareness + the starter's one-way clutch.
- [ ] **Tune crank bounds** on the bench: `MAX_CRANK_MS` (2000), `CRANK_LOSS_ABORT_MS`
  (175), `STARTER_COOLDOWN_MS` (3000). Confirm 2 s is enough for a cold Moster
  start — the pilot can release + re-press to keep cranking (only forced stops
  impose a cooldown). See ADR 0007.
- [ ] **Engine ignition EMI characterization (high priority).** With the receiver
  mounted in its real location next to the engine, sweep the full RPM range and
  log **consecutive** packet losses (not just loss rate) vs RPM. This is the
  failure mode most likely to trip the watchdog in flight: EMI-corrupted packets
  are discarded (safe), but sustained loss in an RPM band ramps throttle toward
  idle. ~14 consecutive losses (175 ms at 80 Hz) is the threshold-A budget. See
  `docs/decisions/0005-radio-choice-and-ignition-emi.md`.
- [ ] **Harden the kill line against EMI** — fail-safe NC kill runs near the CDI;
  coupled noise can pull it toward "kill". `KILL_DEBOUNCE_MS` rejects short
  spikes but not sustained EMI. Add ferrite chokes + twisted/shielded kill
  wiring and route away from the CDI/coil.

## Hardware decisions

- [ ] **Servo selection** — measure pull force/travel across the full stroke
  **through the full installed cable run** (remote mount adds Bowden friction;
  the bare throttle cable understates it) before ordering (~15–25 kg·cm digital
  metal-gear, continuous-duty ballpark). See ADR 0008.
- [ ] **Remote servo mount + cable run (ADR 0008)** — the servo is frame-mounted,
  not on the engine, and drives the throttle via a push-pull/Bowden cable. To
  design: servo bracket, cable spec + routing (avoid tight bends), and slack/
  end-stops so full servo travel = full throttle stroke. `hardware/mechanical/`.
- [ ] **Confirm throttle return-to-idle spring** — the carb spring must reliably
  pull to idle when the servo is depowered/failed or the cable detaches (the
  mechanical fail-safe); verify the linkage can never jam open.
- [ ] **Cable tolerance to engine movement** — the engine shifts on its rubber
  mounts relative to the frame; the cable run must flex without binding or
  shifting the throttle setpoint.
- [ ] **RF range test** — measure real handle↔receiver distance/reliability
  through frame/cage/body to confirm nRF24L01+PA+LNA gives enough margin. NOTE:
  this is a *separate* test from the engine-EMI RPM sweep above — different
  cause (path attenuation vs broadband ignition noise), different fix (band/power
  vs source suppression). Don't conflate the results.
- [ ] **Ignition EMI mitigations** — resistor plug cap/lead, receiver+antenna
  placement away from coil/CDI, ferrites on power/kill leads, CDI/coil grounding,
  receiver supply decoupling. Source suppression first (see ADR 0005).
- [ ] **Channel selection** — pick a channel clear of both local WiFi and the
  engine's worst harmonic bands; consider scanning on boot. Buy the module from a
  reputable supplier (not clones — mismatched PA/LNA erases the margin).
- [x] **Receiver power source** — dedicated receiver battery (isolated from the
  engine's starter battery to avoid crank brown-out / EMI). See ADR 0005.
- [ ] **Servo power architecture (decision)** — servo transients are large; decide
  between (a) servo on the receiver pack via a dedicated BEC/regulator rail with
  bulk capacitance while MCU+radio sit on a cleaner rail, or (b) a separate servo
  battery. Sized after servo selection.
- [ ] **Battery chemistry/voltage per pack** — pick handle + receiver packs, then
  replace placeholder mV values in `HANDLE_BATT` / `RX_BATT` and the
  `read_battery_mv()` divider scaling with measured values.
- [ ] **Engine interface — isolation strategy (decision).** Recommend driving
  kill + starter through **relays or opto-isolated SSRs**, not bare MOSFETs, so
  receiver-ground stays isolated from the noisy engine-ground / starter domain.
  Confirm this before laying out the receiver board.
- [ ] **Kill driver** — relay/opto that grounds the CDI kill wire (parallels the
  independent mechanical kill; energize-to-kill, mechanical path is the zero-power
  fail-safe). Need the Moster 185 CDI kill-wire voltage/behavior to spec it.
- [ ] **Starter driver** — relay/opto to the engine-battery starter *solenoid*
  coil (flyback diode across coil); bounded pulse + cooldown in firmware. Need
  solenoid coil voltage/current.
- [x] ~~Tach-conditioning circuit~~ — **dropped**: no tach in the controller
  (manual crank, ADR 0007). Revisit only if a governor / rev-limiter / auto-idle
  feature is added later.
- [ ] **Connectors** — locking, vibration-rated (JST-SM minimum;
  Deutsch/Amphenol preferred).
- [ ] **Battery readout wiring** — 3/4-LED bar + piezo buzzer per side.
- [ ] **Cruise / accessory switch wiring** — momentary (cruise) + rocker/momentary
  (lights, smoke), all "closed = on" with pull-downs (kill is the exception).

## Firmware TODOs

- [x] Framework chosen: **STM32 HAL + CubeIDE**, Nucleo-32 L432KC dev board
  (ADR 0006). Arduino/STM32duino deliberately not used.
- [ ] Install STM32CubeIDE (bundles CubeMX); ST-Link is built into the Nucleo.
- [ ] Verify Nucleo-32 L432KC has enough pins/peripherals for both ends
  (pin-map exercise — handle I/O is fully known from firmware; receiver I/O
  partly depends on engine facts below). Bump to Nucleo-64 if short.
- [ ] Generate the two CubeMX projects and wire HAL peripheral inits
  (ADC, GPIO, TIM PWM, SPI) marked `/* fill in */`.
- [ ] RF24 HAL port (TMRh20 is Arduino-first) for both ends — stays on the
  critical path given the HAL choice.
- [ ] Servo PWM mapping: 0–255 throttle → pulse-width for the chosen servo.
- [ ] Generate the STM32CubeIDE projects (none committed yet).
- [ ] Open question: does **kill** warrant a confirmed-delivery/ack path, versus
  the current fixed-rate send + latch? See `docs/decisions/0001-no-radio-ack.md`.
- [ ] Low priority: evaluate **CRC8 → CRC16** given the EMI environment (packet
  5→6 bytes). CRC8 is defensible today — sync + seq filtering plus throttle
  rate-limiting contain any single false-accept — but worth revisiting if the
  RPM-sweep test shows heavy corruption. See ADR 0005.

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
