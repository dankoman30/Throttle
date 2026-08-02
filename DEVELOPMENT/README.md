# DEVELOPMENT

Bench-test rigs for bring-up and validation on real hardware, separate from
`src/` (the production firmware), `hardware/` (final board designs), and
`docs/` (project-level docs/ADRs).

Each subfolder here targets one physical unit and is meant to be built and
flashed with STM32CubeIDE against a dev board on a breadboard — proving out
logic before the corresponding real hardware (radio link, relays, production
servo, etc.) is wired up.

```
receiver/      bench rig for the receiver/servo-driver unit (current focus)
transmitter/   bench rig for the handle/trigger unit (deferred until receiver proves out)
```

**Safety note:** nothing under `DEVELOPMENT/` drives a real engine. The
receiver bench rig's kill/starter outputs are LED indicators only — no
relay, no ignition line, no starter solenoid. The mechanical kill switch
requirement in the top-level `README.md` and `hardware/README.md` still
applies to any real installation; it is not superseded by anything here.
