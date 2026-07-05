# Hardware

Schematics, PCB layouts, mechanical designs, and BOM for the throttle system.

Nothing here yet — this tree is scaffolding for upcoming design work:

```
schematics/   handle + receiver schematics (kill line, relays, RF, battery/LED bar, buzzer)
pcb/          board layouts
mechanical/   servo bracket, enclosures, mounting, connector plan
bom/          bill of materials
```

## License

Hardware design files in this directory (once added) are intended to be licensed
under **CERN-OHL-S v2** (CERN Open Hardware Licence, Strongly Reciprocal), which
is written for physical/board designs — separate from the **GPL-3.0-or-later**
that covers the firmware under `src/`. The `hardware/LICENSE` file will be added
alongside the first real design files.

## Safety note

See the top-level `README.md` safety disclaimer. In particular, the **mechanical
kill switch must be wired directly into the ignition kill line, independent of
any microcontroller or radio**, and must function with zero power to the
electronics. It is not — and must not be — controlled by firmware.
