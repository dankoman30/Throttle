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
- [x] ~~Aux-output policy during KILL / loss of signal (smoke force-gating)~~ —
  **moot**: AUX2 (smoke) removed 2026-08-08 to fit the handle's pin budget.
  Only AUX1 (lights) remains, which needs no special kill/loss-of-signal
  policy - `apply_aux_outputs` already mirrors its flag after the safety
  state machine runs, independent of kill/throttle.
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

- [ ] **Handle kill switch is a bench-test stand-in, not the real part.**
  `handle-prod` is currently bench-testing with a normally-open switch
  bridged closed by a jumper wire, purely to unblock testing everything
  else. This project's fail-safe kill design (ADR 0003) requires a
  genuinely **normally-closed** switch/contact block - must be swapped
  before the handle is wired for real. See
  `DEVELOPMENT/handle/handle-prod/docs/wiring.md` "Kill switch".
- [ ] **Servo selection** — measure pull force/travel across the full stroke
  **through the full installed cable run** (remote mount adds Bowden friction;
  the bare throttle cable understates it) before ordering (~15–25 kg·cm digital
  metal-gear, continuous-duty ballpark). See ADR 0008. Fish scale ordered
  2026-08-08 for the force measurement, arriving within a day or two.
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
- [ ] **Receiver battery chemistry/voltage** — pick the receiver pack, then
  replace the placeholder mV values in `RX_BATT` and `read_battery_mv()`'s
  divider scaling with measured values. (Handle-side battery monitoring was
  dropped entirely 2026-08-08 - see below - so this is receiver-only now.)
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
- [x] ~~Battery readout wiring — 3/4-LED bar + piezo buzzer per side~~ —
  **superseded 2026-08-08**: the handle has no onboard battery sense at all
  now (standalone battery meters on the packs themselves, not wired to the
  board); the receiver folds low-battery into its tri-color status LED's
  blink rate instead of a dedicated bar/buzzer (see `prod_app.c`). The
  bar/buzzer math in `battery_monitor.h` (`battery_eval`/`battery_buzzer_on`)
  stays as general-purpose, unit-tested logic but isn't wired to real
  hardware on either board.
- [ ] **Cruise / accessory switch wiring** — momentary (cruise) + rocker/momentary
  (lights), all "closed = on" with pull-downs (kill is the exception). (AUX2/
  smoke removed 2026-08-08 to fit the handle's pin budget - only one
  accessory switch now.)

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
  `DEVELOPMENT/receiver/receiver-prod/`'s TIM2 (50 Hz servo PWM, `PA5`) is
  currently set up for a **placeholder SG90 micro servo** (bench-test only,
  not the final actuator): Prescaler=31, Period=19999 (these two just set the
  50 Hz frame rate and should hold for any standard-protocol servo,
  metal-gear included), Pulse=1500 (1.5 ms, SG90 center). Once the real servo
  is selected (see "Servo selection" above), revisit the pulse-width
  min/max mapped to full-closed/full-open throttle — likely just a firmware
  constant change, not a CubeMX regeneration, unless the new servo uses a
  non-standard control protocol.
- [ ] **Trigger-ADC anti-alias + oversampling.** The trigger is sampled at 80 Hz,
  so hand/engine vibration above ~40 Hz **aliases** and no software filter can
  remove it. Add (hardware) an **RC low-pass on the ADC input** as an anti-alias
  filter, and (firmware) **oversample the ADC faster than 80 Hz and average**
  before the EMA. The handle already has an EMA + a deadband/hysteresis
  (`THROTTLE_DEADBAND`) and the receiver rate-limits the servo, but those sit
  *after* sampling — they don't fix aliased noise. Tune the EMA `FILTER_SHIFT`
  and `THROTTLE_DEADBAND` on real hardware. (Raised by instructor feedback on
  servo-from-analog control: debounce, damping, hysteresis.)
- [ ] Generate the STM32CubeIDE projects (none committed yet).
- [x] ~~Open question: does **kill** warrant a confirmed-delivery/ack path~~ —
  **resolved: no.** Kill's latch+resend already survives arbitrary packet
  loss on its own, and a fully-dead link (the only case ACK could help) is
  already covered by the independent mechanical kill line. Auto-ack also
  risks blocking sends longer exactly under the ignition-EMI condition ADR
  0005 already flags as the dominant threat. See the 2026-08-02 addendum in
  `docs/decisions/0001-no-radio-ack.md`.
- [ ] **Telemetry downlink (receiver → handle), deferred.** Receiver battery
  level and receiver state currently have no path to the pilot at all — worth
  building eventually, but as a separate, strictly one-way, structurally
  isolated feature (own packet type/pipe, never able to delay or gate a
  control packet), after the primary control link is working. Not a
  flight-readiness blocker on its own. **When built, the handle should show**
  its own battery level side by side with the receiver's (telemetered), plus
  receiver system status (cranking/idle-armed/killed) — see the 2026-08-08
  addendum in `docs/decisions/0001-no-radio-ack.md` for the full reasoning
  and transport constraints.
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
