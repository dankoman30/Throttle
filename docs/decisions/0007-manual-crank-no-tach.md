# 0007 — Manual cranking (no tach in the controller)

**Status:** accepted (supersedes the RPM-based engine-caught approach) · **Date:** 2026

## Decision

Electric start is **manual**: the receiver energizes the starter while the pilot
holds the (hold-confirmed) start button and releases it when the engine catches.
The pilot — who hears the engine and already runs a **separate RPM gauge** — is
the "engine caught" sensor. There is **no tach wired into the receiver** and no
RPM-based `STARTING → RUNNING` transition.

## Why

- A paramotor electric start is normally a held-button affair; the human is a
  perfectly good catch sensor, and the pilot's existing RPM readout means a tach
  into the controller adds no information they lack.
- Removes a whole EMI-coupled subsystem: the plug-lead pickup shares the
  ignition's broadband noise (ADR 0005), plus a tach-conditioning circuit.
- Removes two guards that were never fail-safe: RPM-based catch detection and the
  `rpm == 0` "don't crank a turning engine" gate — both failed *permissive* on a
  tach fault or an MCU reset.
- Simpler receiver state machine (`STATE_RUNNING` dropped).

## How the crank is bounded (it can't run away)

- **Rising-edge start** — must release and re-press to re-crank; after a forced
  cutoff, holding the button does nothing.
- **`CRANK_LOSS_ABORT_MS` (~175 ms)** — stop if the link goes silent mid-crank
  (can't command kill wirelessly while blind).
- **`MAX_CRANK_MS` (~2 s)** — hard backstop even if the button is held or
  `START_REQ` sticks asserted.
- **`STARTER_COOLDOWN_MS`** applies only after a *forced* stop; a normal release
  imposes no cooldown, so re-cranking a stubborn cold engine is instant.
- Plus the existing kill-first ordering and the main-loop safety net that forces
  the starter off whenever `state != STARTING`.

## Tradeoffs accepted

- No automatic starter release on catch — the pilot releases (the starter's
  one-way clutch and the max-crank cap mitigate over-cranking).
- No firmware protection against cranking an already-running engine (e.g. after a
  pull start) — relies on pilot awareness + the starter clutch.

## Reconsider if

We later want a **governor / rev-limiter / auto-idle-on-engine-death** — those
need RPM the pilot can't provide and would reintroduce a (well-conditioned) tach
input. Deferred; not needed for v1.
