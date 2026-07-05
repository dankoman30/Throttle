# 0008 — Remote-mounted servo, cable-driven throttle

**Status:** accepted · **Date:** 2026

## Decision

The throttle servo is **remote-mounted** — placed on the frame/cage, **not**
bolted at the engine's throttle/carburetor — and drives the engine throttle arm
through a **push-pull / Bowden cable**. The servo does not sit at the point where
the cable meets the engine.

## Why

- **Space:** the area at the carb throttle is cramped; remote mounting gives real
  placement freedom for the servo, receiver, wiring, and antenna.
- **Vibration:** keeping the servo off the engine spares it the worst of the
  2-stroke vibration — better servo life and fewer failures.
- **Integration:** lets the receiver/antenna sit where RF, cooling, and cabling
  are easiest.

## Safety property (fail-safe direction)

The carburetor's **return spring pulls the throttle to idle**; the servo pulls
*against* it to open. So a depowered servo, a servo failure, or a broken/detached
actuation cable lets the spring return the engine to **idle** — the mechanical
counterpart of the loss-of-signal watchdog. The throttle must have a positive
return-to-idle spring and the linkage must never be able to jam open.

## Consequences to design for

- **Friction/stiction:** a Bowden cable adds drag. Measure servo pull force
  across the full stroke **through the actual installed cable run**, not the bare
  throttle cable, or the servo will be undersized.
- **Routing:** avoid tight bends (friction), secure the cable against vibration.
- **Engine movement:** the engine shifts on its rubber mounts relative to the
  frame; the cable run must tolerate that flex without binding or changing the
  throttle setpoint.
- **Full stroke:** servo travel must still map to the full throttle stroke after
  any cable slack/stretch is taken up (set end-stops/travel accordingly).

Mechanical details (bracket, cable spec, routing) live under `hardware/mechanical/`.
