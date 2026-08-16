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
  **resolved: no special policy, by deliberate choice.** AUX2 (smoke) was
  removed 2026-08-08 then restored 2026-08-09 once pins freed up elsewhere
  on the handle; revisiting this question on restore, the decision was to
  keep both AUX1 and AUX2 fully out of the kill/start/throttle state
  machine - `apply_aux_outputs` mirrors both flags unconditionally, same as
  before. Not an oversight - discussed and confirmed 2026-08-09.
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
- [x] **Multi-unit isolation — prevent a foreign handle from controlling this
  receiver.** **Mechanism implemented 2026-08-14** (see below for the
  per-build discipline this still requires - that part is intentionally
  never "done"). Previously, every handle/receiver pair built from this
  code used the *identical* hardcoded nRF24 address (`PLACEHOLDER_ADDR` =
  `0xE7E7E7E7E7`) and channel (`PLACEHOLDER_RF_CHANNEL` = 76, see
  `src/common/nrf24.c`). Nothing in the packet (`sync`/`seq`/`throttle`/
  `flags`/`crc8`) carries any device identity, so two pilots' rigs within
  radio range were indistinguishable to each other's receivers — a
  receiver would accept a well-formed KILL/START/THROTTLE packet from
  *any* handle, not just its own. **Fix:** each manufactured handle+
  receiver pair gets a unique 5-byte nRF24 address, burned into both
  firmwares as a compile-time constant (like a serial number), replacing
  the placeholder — hardware-level address matching
  means a foreign packet is never even received by the MCU, no packet format
  change needed, no runtime pairing handshake (consistent with ADR 0001's
  no-ack rationale and this project's compile-time-constants-only
  philosophy). **Compile-time vs. a hardware DIP/rotary switch for setting
  the address in the field was explicitly weighed and decided against - see
  ADR 0009** (no spare pins on the handle to read a switch bank, and a
  physical switch is a real vibration-induced failure mode on this
  vehicle). Considered and set aside as the primary fix: per-pair channel
  (channel is a scarcer resource already earmarked for interference avoidance
  below — don't conflate the two; could still be a bonus secondary layer) and
  a device-ID in the 3 reserved `flags` bits (software-only check that runs
  after the packet is already received, and only 8 distinct values — not
  enough for a real fleet; leave those bits reserved). **Allocation scheme
  decided (recording only — not yet wired into firmware):** address =
  `0xE7 0xE7 0xE7 0xE7 <N>`, where `<N>` is a one-byte sequential unit number.
  `0x00` stays reserved as an "unassigned/placeholder" marker (so a unit still
  on `PLACEHOLDER_ADDR` is obviously unfinished) and `0xFF` is reserved for a
  possible future broadcast/test address; assignable range is `0x01`–`0xFE`
  (254 pairs). Numbers are assigned sequentially — never reused, even for a
  retired unit — and logged the moment they're assigned in the append-only
  `docs/UNIT-REGISTRY.md`. **Build-time mechanism (resolved):** a per-unit
  config header, `src/common/unit_config.h` — **tracked in git**, default
  `UNIT_NUMBER 0xFF` (deliberate unpaired bench/test build marker, not the
  reserved-but-refused `0x00`). `nrf24_handle_t` gained an `addr` field
  (same parameterized-not-hardcoded pattern as `hspi`/`ce_port`/etc.);
  `handle_app.c`/`prod_app.c` each build their 5-byte address from
  `UNIT_NRF24_ADDR_PREFIX` + `UNIT_NUMBER` and set it before calling
  `nrf24_init()`. `UNIT_NUMBER == 0x00` is a hard **compile-time `#error`**
  — the build itself refuses to produce a binary if the placeholder was
  never edited, not just a warning. Test builds (`UNIT_NUMBER == 0xFF`)
  also get a visual "this is not a paired unit" cue: both boards' status
  LED heartbeat renders **inverted** (mostly on with brief off blips,
  instead of mostly off with brief pulses) — see `UNIT_IS_TEST_BUILD` in
  `unit_config.h`.
- [ ] **⚠️ STANDING REMINDER — confirm `UNIT_NUMBER` before every real
  build. Intentionally never checked off.** Before flashing a unit meant
  to actually ship/fly as a real paired production unit, open
  `src/common/unit_config.h` and confirm `UNIT_NUMBER` is set to that
  pair's real assigned number from `docs/UNIT-REGISTRY.md` — **not** the
  tracked default (`0xFF`). The compile-time `#error` only catches the
  `0x00` case (file never touched at all); it cannot tell a genuine
  registry number apart from a left-at-`0xFF` bench build someone forgot
  to change before flashing real hardware. After flashing, revert the
  local edit (or otherwise make sure the real per-unit value never gets
  committed back to this file — see the comment in `unit_config.h`
  itself). This line should stay in this document, unchecked, permanently
  — it is a process discipline check, not a one-time task.

## Hardware decisions

- [ ] **Production PCB approach — Nucleo carrier board vs. bare STM32L432KC.**
  Need to decide, before ordering from PCBWay (or similar), whether production
  `handle-prod`/`receiver-prod` units keep the Nucleo-32 L432KC dev board
  soldered onto a custom carrier PCB that just breaks out its pins, or move to
  a from-scratch board built around the bare STM32L432KC chip. The bare-chip
  route drops dev-board cost/size but takes on power supervisor, crystal,
  ST-Link/debug header, and boot-mode strapping that the Nucleo currently
  provides for free (ADR 0006 chose the Nucleo for bring-up; this is the
  separate question of what production actually ships on).
- [ ] **KiCad schematics + ERC for production handle/receiver boards.** Only
  `receiver-bench` has a KiCad project today
  (`DEVELOPMENT/receiver/schematics/kicad/receiver-bench/`), and it's the
  bench rig, not `handle-prod`/`receiver-prod`. Once the Nucleo-carrier-vs-
  bare-chip decision above is made, need real schematics for both production
  boards and a KiCad ERC pass (unconnected pins, driver conflicts, missing
  power flags) before sending anything to PCBWay.
- [ ] **Production throttle (trigger) position sensor — rotary vs. linear,
  no leading candidate yet.** handle-prod's trigger is currently wired to a
  rotary pot (see `DEVELOPMENT/handle/handle-prod/docs/wiring.md` "Trigger"),
  but that's the bring-up part, not a production decision - open question is
  what sensor type actually ships in the production trigger mechanism: rotary
  pot (simplest, matches what's already proven on the bench) vs. linear/slide
  pot (may suit a different trigger-lever geometry or travel feel better).
  Need to figure out where to even start evaluating this.
  - **Follow-up once the real sensor is picked: recalibrate
    `read_throttle_position()`'s ADC-to-throttle mapping.** Bench-tested
    2026-08-15 on the current pot (after adding the RC anti-alias filter):
    raw ADC reads a clean, stable `0` at full release but only `~4030` at
    full pull, not the `4095` the mapping's `(smoothed * 255) / 4095`
    formula assumes - meaning full trigger pull currently commands ~98%
    throttle (`mapped` caps around 250), never a true 255, since the
    deadband's "always update at the rails" rule only forces an update at
    the literal 0/255 values. Don't fix this for the bench pot - not
    worth calibrating a part that isn't shipping. Once the real sensor is
    selected, measure *its* actual min/max on the bench and scale the
    mapping off those measured values instead of assuming rail-to-rail.
- [ ] **Handle kill switch is a bench-test stand-in, not the real part.**
  `handle-prod` is currently bench-testing with a normally-open switch
  bridged closed by a jumper wire, purely to unblock testing everything
  else. This project's fail-safe kill design (ADR 0003) requires a
  genuinely **normally-closed** switch/contact block - must be swapped
  before the handle is wired for real. See
  `DEVELOPMENT/handle/handle-prod/docs/wiring.md` "Kill switch".
- [~] **Servo selection** — measure pull force/travel across the full stroke
  **through the full installed cable run** (remote mount adds Bowden friction;
  the bare throttle cable understates it) before finalizing (~15–25 kg·cm digital
  metal-gear, continuous-duty ballpark). See ADR 0008. Fish scale arrived
  2026-08-16, force/travel measured on the existing handle cable (see below).
  **Prioritize speed/slew rate (deg/sec), second only to safety** - the
  current SG90 placeholder's own mechanical response is a likely major
  contributor to the throttle latency observed on the handle-prod bench
  test (2026-08-09; AUX1, which has no filtering/rate-limit pipeline,
  responded far faster than the servo-driven throttle). Pick a servo with
  a genuinely fast rated speed, not just adequate torque - a strong but
  slow servo would undermine responsiveness even with adequate pull force.
  **Update (2026-08-15):** ruled out software as the cause. The full
  handle→servo chain's timing budget (EMA settle ~65ms + receiver rate
  limiter ~42ms + 50Hz PWM update ~20ms) is well under 100ms, nowhere near
  the lag observed. Confirmed with the SG90 currently **unloaded** (no
  cable/spring attached): response is near-instant and moves at a steady
  speed to the target - not a torque-fighting-load deceleration, just the
  servo's own slew rate being slower than wanted. Since this is unloaded,
  it isn't even worst-case - the real installed cable+spring load will
  likely be at least this slow, probably slower.
  - [ ] **Fish-scale force + travel measurements taken (2026-08-16), on the
    existing handle-actuated cable — not yet through the future
    remote-mount Bowden run.** Per ADR 0008 this is a floor, not final:
    re-verify once the real cable routing exists, since Bowden friction
    isn't included here.
    - **Force:** ~2 lbf breakaway (start of cable movement), rising
      smoothly to ~5 lbf at 95-98% open, ~6 lbf at fully open. Budget
      7-8 lbf as a safety margin for spec'ing torque.
    - **Travel:** 1/4" slack at the lever before cable movement starts,
      then 7/8" of loaded cable travel to fully open - **1-1/8" (1.125")
      total linear pull** the servo mechanism must produce end to end.
    - **Torque/horn-arm sizing:** servo torque (kg·cm) = force (lbf) ×
      horn radius (in) × 1.152; rotation angle needed ≈ (throw ÷ radius)
      in radians. A 0.75"-1.0" horn radius needs only ~65-86° of
      rotation (comfortable margin below a standard ~120° usable range,
      room for end-stops) at a modest 6.9-9.2 kg·cm even at the 8 lbf
      margin figure - well under the original 15-25 kg·cm ballpark above.
      **Torque is no longer the binding constraint** - free to prioritize
      speed (deg/sec) per the update above.
    - **Candidates researched (2026-08-16).** First pass: Hitec
      **HS-5645MG** (12.1 kg·cm @ 6V, 0.18 sec/60° ≈ 333°/sec) and Hitec
      **D645MW** (11.4-13.0 kg·cm @ 6-7.4V, 0.17-0.20 sec/60° ≈ 300-353°/sec)
      - both explicitly marketed for throttle/control-surface (sustained
      holding) duty rather than RC-car burst duty, which is the key filter
      versus cheap generic "high torque" servos (e.g. MG996R/DS3218MG
      class) that are commonly reported to run hot/fail under continuous
      load. **Passed over after checking reviews** - HS-5645MG has
      recurring complaints of gear backlash/slop and centering drift (some
      out of the box) and eventual geartrain stripping; D645MW had thin
      review data plus one failure report (shock-load bashing use, not
      directly comparable, but not reassuring).
    - **Decided/ordered (2026-08-16): Savox SC-1256TGP.** 16 kg·cm @ 4.8V
      / 20 kg·cm @ 6V (comfortable margin above target, absorbs the
      not-yet-measured Bowden friction), 0.18 sec/60° @ 4.8V / 0.15 sec/60°
      @ 6V (≈400°/sec - faster than both Hitec candidates), coreless motor,
      titanium gears, **aluminum center case specifically for cooler
      running** (directly addresses the continuous-duty/overheating
      concern, not just a torque/speed number), consistently positive
      reviews with no backlash/centering complaints found. Ordered;
      fish-scale numbers still need re-verification through the real cable
      run once it exists (see above) before finalizing pulse-width mapping.
- [ ] **Remote servo mount + cable run (ADR 0008)** — the servo is frame-mounted,
  not on the engine, and drives the throttle via a push-pull/Bowden cable. To
  design: servo bracket, cable spec + routing (avoid tight bends), and slack/
  end-stops so full servo travel = full throttle stroke. `hardware/mechanical/`.
- [ ] **Confirm throttle return-to-idle spring** — the carb spring must reliably
  pull to idle when the servo is depowered/failed or the cable detaches (the
  mechanical fail-safe); verify the linkage can never jam open. May need a
  stiffer/different return spring depending on the selected servo's own
  unpowered ("limp") holding force - a spring sized for one servo's drag
  might not reliably overcome a different one's. Measure/select together
  with the servo, not independently.
- [ ] **Cable tolerance to engine movement** — the engine shifts on its rubber
  mounts relative to the frame; the cable run must flex without binding or
  shifting the throttle setpoint.
- [ ] **Vibration dampeners for receiver mounting (2026-08-14).** The receiver
  enclosure — and whatever battery ends up inside it, see the cell-format item
  below — needs isolation from engine vibration, not just a rigid mount to the
  frame. Evaluate dampening grommets/standoffs or an isolating mount plate
  between the receiver enclosure and the frame.
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
  (Scoped to interference avoidance only — see "Multi-unit isolation" above
  for preventing cross-talk between two pilots' rigs.)
- [x] **Receiver power source** — dedicated receiver battery (isolated from the
  engine's starter battery to avoid crank brown-out / EMI). See ADR 0005.
- [ ] **Servo power architecture (decision)** — servo transients are large; decide
  between (a) servo on the receiver pack via a dedicated BEC/regulator rail with
  bulk capacitance while MCU+radio sit on a cleaner rail, or (b) a separate servo
  battery. Sized after servo selection.
  **Current leaning (2026-08-09, not finalized):** receiver + servo on a
  **2S Li-ion pack** (7.4–8.4V) through a buck/BEC down to 5V for both the
  servo and the board; handle on a **single 1S Li-ion cell** (3.7–4.2V)
  through a boost module, since it has no servo and just needs clean
  3.3–5V logic power (cheap combined boost+USB-C-charge modules exist for
  exactly this). Cell **format** (cylindrical 18650 vs. pouch LiPo) is a
  separate, still-open decision — see the battery cell format item below.
  Real open sub-questions, not yet resolved:
  - Whether the Nucleo-32 L432KC's 5V/VIN pins tolerate direct external
    injection when not USB-powered — needs checking against the datasheet,
    not assumed.
  - BEC/boost current sizing depends on the still-unselected servo's
    stall/running current.
  - **Runtime target: ≥2 hours of continuous servo operation** — a real
    sizing constraint for both pack capacity and servo current draw once
    the servo is picked.
- [ ] **Battery cell format — cylindrical (18650) vs. pouch/soft-cell LiPo,
  still open (2026-08-14).** Cell count/voltage leaning is unchanged (1S
  handle, 2S receiver — see "Servo power architecture" above); the physical
  cell format is a separate, unresolved blocker before packs can be sourced
  or a battery compartment/mount designed for either unit.
  - **Cylindrical 18650**, swapped as loose individual cells — hard can gives
    better mechanical/thermal robustness than pouch, which matters more here
    given the receiver's engine-adjacent, high-vibration mount (see the
    vibration-dampener item above). Main risks: spring-contact reliability
    under vibration (needs contact preload + secondary retention, not just
    the spring), reverse-insertion risk (needs physical keying, not a
    warning label alone), and no cell-level protection unless using
    protected 18650s specifically.
  - **Pouch LiPo, 2-lead (no balance tap), one pack per cell** — soldered
    leads + a keyed connector (JST/XT) solve the contact-reliability and
    reverse-polarity risks cleanly, but pouch cells are more puncture/crush-
    sensitive and carry more thermal-runaway risk than a hard can, which
    weighs heavier here than in a typical RC application given the
    receiver's heat/vibration exposure near the engine. Would need
    reputable-brand packs (real datasheet, trustworthy mAh rating — matters
    for hitting the ≥2 hr runtime target) and likely a fireproof-bag storage/
    charging practice in production if this is the format chosen.
  - **Either format:** always charge each cell individually to full before
    combining in series — this is what avoids needing a 2S balance charger/
    tap at all, regardless of which format is picked. Packs/cells removable
    and charged externally either way — no onboard charge circuitry planned
    for either unit.
  - **Decision deferred; actual battery choice may still change.** Don't
    finalize the battery-compartment mechanical design or order production
    cells until this is resolved.
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
  hardware on either board. The "standalone battery meters" referenced here
  are now on hand - see the next item.
- [ ] **Battery capacity indicator modules ordered (2026-08-09).** Two
  kinds, one for each pack, both purely passive - 2-wire, connected straight
  across the battery terminals, no GPIO/ADC pins on either board and no
  firmware involvement at all:
  - Generic "1-8S universal" module: select the cell-count pad (S1-S8,
    solder-bridge one only) to match the pack; red 7-segment-style digits,
    4-level blue/green bar display. 3-34V working range, 5mA draw,
    -20-50°C. Not waterproof.
  - DGZZI 1S-specific module: fixed for a single cell (3.7-4.2V), 4-level
    red/orange/green bar (red ~5%, orange ~50%, green 75-100%). Also not
    waterproof.
  - Neither needs a decision before wiring - just connect to whichever pack
    ends up on each board once "Servo power architecture" above is settled.
- [ ] **Charging board compatibility (note for purchasing).** 1S and 2S
  packs need **different** charge modules - not interchangeable. A
  1S-rated board (e.g. common TP4056-style USB-C modules) caps out around
  4.2V and will undercharge a 2S pack; a 2S-rated charge board outputs
  ~8.4V and would overcharge a single cell. Series vs. parallel matters
  specifically for charging (parallel keeps it single-cell-equivalent
  voltage; series does not). For the 2S receiver pack, also get a board
  with a **balance** tap/connector - a plain series-only charger without
  balancing lets the two cells drift apart in capacity over many cycles,
  which shortens pack life and risks over-discharging one cell.
- [x] **Cruise / accessory switch wiring** — momentary pushbuttons for cruise,
  AUX1 (strobe), and AUX2 (smoke), all "closed = on" with pull-downs (kill
  and start are the pull-up exceptions). AUX1 is latched in firmware
  (press once for on, again for off); AUX2 stays purely momentary. All
  three confirmed working on handle-prod real hardware 2026-08-09. (AUX2
  was briefly removed 2026-08-08 for the handle's pin budget, restored
  2026-08-09 once pins freed up elsewhere on that board.)

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
- [~] **Trigger-ADC anti-alias + oversampling.** The trigger is sampled at 80 Hz,
  so hand/engine vibration above ~40 Hz **aliases** and no software filter can
  remove it. Needs both a hardware and a firmware half - the EMA + deadband/
  hysteresis (`THROTTLE_DEADBAND`) already in the handle and the receiver's
  servo rate-limiter all sit *after* sampling, so none of them can fix
  aliased noise; it has to be addressed at the sampling stage itself.
  - **Firmware half done (2026-08-14):** `read_throttle_raw_oversampled()`
    in `src/handle/handle_firmware.c` averages `TRIGGER_OVERSAMPLE_COUNT`
    (8, `src/common/throttle_protocol.h`) back-to-back raw ADC conversions
    into one sample before the EMA ever sees it. Helps on its own, but is
    not a substitute for the hardware half below - oversampling alone
    can't undo aliasing that already happened in the analog domain.
  - **Hardware half in progress (2026-08-14):** an **RC low-pass on the
    ADC input** (`PA5`/`TRIGGER_ADC` on `handle-prod`) as the actual
    anti-alias filter. **R=1kΩ, C=2.2µF electrolytic (50V, on hand) →
    cutoff ≈72Hz** - deliberately chosen below the original ~100Hz
    estimate: comfortably below the Moster 185's ~120-130Hz fundamental
    vibration frequency at max RPM (more attenuation margin there), while
    still far above any realistic hand-trigger input speed, so nothing is
    lost on the input side. Goes in series between the pot wiper and the
    pin, capacitor from that same node to GND (coexists fine with the
    existing 100kΩ fail-safe pull-down already there - see
    `DEVELOPMENT/handle/handle-prod/docs/wiring.md` "Trigger"). Electrolytic
    is polarized - positive lead to the filter node (`PA5` side), negative
    to GND (this node is always ≥0V here, so polarity is well-defined).
    **Must be paired with a longer ADC sampling time** in
    CubeMX for the trigger channel (currently `ADC_SAMPLETIME_2CYCLES_5`,
    sized for ~zero source impedance) - the new series R needs a much
    longer sample window for the ADC's sample-and-hold capacitor to
    charge fully, or readings silently read low. Treat the RC filter and
    the sampling-time change as one combined change, and verify a known
    trigger position (e.g. full release ≈ 0) reads correctly afterward.
  - **Swap to a proper ceramic cap later (not urgent).** The 2.2µF
    electrolytic above works fine and unblocks bring-up now, but a
    ceramic (X7R, 1-2.2µF, 16V+) would be the more correct part for a
    small-signal analog filter - lower leakage/ESR, unpolarized (no
    reverse-wiring risk). On-hand ceramics were only 0.1µF ("104"), too
    small to reach ~72Hz without raising R enough (~22kΩ) to create a
    real voltage-divider loading problem against the existing 100kΩ
    pull-down (~19% signal attenuation) - not worth it. Order the right
    value instead of working around it; keep R at 1kΩ when swapping so
    the loading math doesn't need to be redone.
  - [x] **Tuned the EMA `FILTER_SHIFT` and `THROTTLE_DEADBAND` on real
    hardware (2026-08-15, `feature/ema-deadband-tuning`).** With the RC
    filter + oversampling now doing the primary anti-alias job upstream,
    relaxed both: `FILTER_SHIFT` 2→1 (EMA settling ~130ms→~65ms) and
    `THROTTLE_DEADBAND` 3→1 (finer resolution, justified by the near-zero
    jitter measured at held trigger positions in the bench debugger data).
    Small, borderline-perceptible improvement on the bench - most of the
    remaining latency turned out to be the SG90 test servo's own slew
    rate, not filtering; see "Servo selection" below. (Raised by
    instructor feedback on servo-from-analog control: debounce, damping,
    hysteresis.)
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
