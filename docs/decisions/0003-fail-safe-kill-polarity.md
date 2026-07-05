# 0003 — Fail-safe kill polarity (and why kill differs from every other input)

**Status:** accepted · **Date:** 2026

## Decision

Each input's **default state (disconnected / broken wire / glitch) must equal
that function's safe state.** Concretely:

| Input | Safe state | Wiring |
|---|---|---|
| **Kill** | engine off | **normally-closed + pull-up** — open/broken = HIGH = kill |
| Start | not cranking | normally-open, closed = start |
| Throttle (ADC) | idle | pull-down, open = 0 |
| Cruise | disengaged | normally-open, closed = engage |
| Aux (lights/smoke) | off | normally-open, closed = on |

Kill is the deliberate odd one out: it is wired the **reverse** polarity from
everything else, so a severed kill wire *kills* rather than silently leaving the
pilot unable to kill.

## Why

For a safety kill, a *missed* kill is dangerous while a *spurious* kill is merely
a safe engine-off (paramotor pilots train for engine-out). So kill is biased
toward "too eager to stop" — the e-stop convention (ISO 13850): safety stops are
normally-closed and a broken wire triggers the stop. Every other input is biased
the opposite way: a broken wire must not crank the engine, engage cruise, or
raise throttle, so those are energize-to-act with pull-downs.

## Consequence

Because the handle **latches** kill, a transient glitch on the NC line could
latch a nuisance shutdown. Mitigated by `KILL_DEBOUNCE_MS` (30 ms
persist-before-latch), which rejects vibration glitches while still latching a
real press or a genuinely severed wire. Good locking connectors do the rest.

Implemented in `src/handle/handle_firmware.c` (`read_kill_switch`,
`kill_confirmed`); the receiver is unaffected (it only acts on `CMD_FLAG_KILL`).
