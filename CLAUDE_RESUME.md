# Session resume notes

**Purpose of this file:** a snapshot of exactly where things stood at the end
of the last working session, for whoever (human or Claude) picks this back
up — likely once the radio hardware ordered below arrives. Read `CLAUDE.md`
first for the project itself; this file is just "where did we leave off."
Safe to delete/rewrite once it's stale — it's a handoff note, not permanent
documentation.

**As of:** 2026-08-02.

## Git state

Two branches, both pushed to `origin`:
- **`next`** — the integration branch. Everything below except the radio
  hardware-ordering docs lives here.
- **`feature/rf24-hal-port`** — radio bring-up work specifically, currently
  rebased on top of `next` (contains 3 commits on top: the radio bring-up
  plan doc and its two updates as hardware got ordered).

**Standing workflow policy** (also saved in Claude's memory, but noted here
in case a fresh session doesn't have that): whenever a commit lands directly
on `next` while `feature/rf24-hal-port` exists, rebase the feature branch
onto the updated `next` afterward. Rebasing rewrites the feature branch's
commits, so pushing that update needs `git push --force-with-lease` —
confirm with the user before force-pushing even though the rebase itself is
routine at this point.

## What's done

**Receiver bench rig** (`DEVELOPMENT/receiver/`) — complete and verified on
real hardware. Runs the actual `src/receiver/receiver_firmware.c` state
machine (via `#include`, one-line `millis()` fill-in only) on an STM32L432KC
Nucleo-32, fed by breadboard buttons/pot instead of the radio. Full
verification checklist passed 2026-08-01/08-02 — see
`DEVELOPMENT/receiver/docs/bench-behavior.md`. Required a real hardware mod
(SB16/SB18 solder bridges removed) — see "Gotchas" below.

**Receiver bench documentation** — wiring, CubeMX config, behavior spec, and
a Mermaid wiring diagram (`DEVELOPMENT/receiver/schematics/receiver-bench.md`,
renders natively on GitHub, no software needed) are all current and match
the built hardware.

**Design docs updated this session:**
- `docs/PROJECT_DESIGN.md` — added a full "Tunable Constants Reference"
  table (every `#define` in `throttle_protocol.h`/`battery_monitor.h`, what
  each one trades off); fixed a stale `RUNNING`-state reference.
- `docs/decisions/0001-no-radio-ack.md` — 2026-08-02 addendum: control
  uplink stays one-way/unacked (kill's own latch+resend plus the mechanical
  kill line already cover what ACK would add; auto-ack risks blocking sends
  longer under the ignition-EMI conditions ADR 0005 already flags as the
  dominant threat). A receiver→handle telemetry downlink is a legitimate
  future want (receiver battery/state currently have no path to the pilot)
  but deferred, and must stay structurally isolated from the control path
  if built.
- `docs/OPEN-ITEMS.md` updated to match.

**KiCad schematic** (`DEVELOPMENT/receiver/schematics/kicad/receiver-bench/`)
— electrically complete and verified, **0 ERC errors**. Built via a mix of
guided GUI walkthrough (user placed the potentiometer themselves, first
component, to validate the approach) and direct file authoring (everything
else — I don't have KiCad installed in this environment, so correctness was
checked by (a) the user running ERC in KiCad and reporting results back, and
(b) me writing small Python scripts to parse the file and verify pin↔label
coordinates programmatically rather than trusting my own arithmetic).
Contains: pot, 3 buttons, 7-segment display (7× LED+resistor, common
cathode), 4 status LEDs, buzzer, servo connector, and — as of the last
commit — the **real NUCLEO-L432KC board symbol** (SnapEDA import, both
CN3/CN4 header units placed as `U1`), with all 17 used MCU pins genuinely
wired to their matching peripheral-side labels (verified by script, not
eyeballing).

125 ERC warnings remain, all cosmetic/expected: off-grid coordinates
(doesn't affect correctness), simplified LED/switch/buzzer/connector symbol
graphics vs. KiCad's stock library art (deliberate — lower risk to
hand-author than matching exact stock geometry), and — before the MCU
symbol was wired in — single-use labels (mostly resolved now that each
label has a real MCU-pin endpoint too).

**Open/unresolved on the schematic:** the area around the MCU symbol's pins
is visually cluttered (three overlapping text elements per pin: KiCad's pin
name, pin number, and my added label, at only 2.54mm pin spacing). I
tried hiding the MCU's pin numbers as a fix; the user asked me to undo it
before deciding — **no layout fix has been agreed on yet**. Worth revisiting
if/when the user wants to actually read that part of the schematic
comfortably. Options not yet explored: longer pin-to-label wire stubs,
spacing pins out more (not possible — fixed by the real connector pinout),
or just accepting the clutter since electrical correctness doesn't depend
on it.

**Radio link — design decided, hardware ordered, nothing built.** Bring-up
plan, hardware rationale, and a real gotcha already caught (STM32L432KC's
*default* SPI1 pins are `PA5`/`PA6`/`PA7` — exactly the SB16/SB18-bridged
pins; use the `PB3`/`PB4`/`PB5` alternate mapping instead) are fully
documented in **`DEVELOPMENT/radio/README.md`** — read that file first when
resuming this thread. Ordered: 4× nRF24L01+PA+LNA (Addicore), 2 additional
Nucleo-32 L432KC boards (→ 4 total, only 1 has SB16/SB18 removed), an
electrolytic + ceramic capacitor assortment kit, and a LONELY BINARY
8-channel 24MHz logic analyzer (pair with sigrok/PulseView, not vendor
software).

## What's next (once hardware arrives)

Follow the bring-up order already written in `DEVELOPMENT/radio/README.md`:
1. Single board + single nRF24 module: confirm SPI works at all via a
   register read-back (e.g. `CONFIG`, datasheet default `0x08`). Use the
   logic analyzer on SCK/MOSI/MISO/CSN/CE to sanity-check against the
   datasheet timing diagrams.
2. **Decide**: adapt an existing STM32 port of TMRh20's RF24 library, or
   write a minimal custom driver against the datasheet (only need: fixed
   5-byte writes, poll-based `available()`/`read()`, no ack, no dynamic
   payload — see the doc for the specific tradeoff reasoning). Decide this
   once SPI is proven solid, not before.
3. Two boards + two modules: raw TX/RX smoke test, independent of either
   firmware file.
4. Only then: wire real `radio.*` calls into `handle_firmware.c` and
   `receiver_firmware.c` (currently fully commented-out stubs) and
   re-verify both bench rigs end-to-end over real radio.

Also still open, lower priority: the `DEVELOPMENT/transmitter/` bench rig is
still just a placeholder (deferred until the receiver rig proved out, which
it now has — could be picked up independently of the radio work).

## Gotchas worth remembering

- **SB16/SB18 solder bridges**: this Nucleo-32 L432KC revision ships with
  two SMD 0Ω resistors closed by default, internally tying `PA6`↔`PB6` and
  `PA5`↔`PB7`. Caused a multi-day "kill button reads stuck" mystery on the
  receiver bench rig. Removed on that one board only — the other 3 in
  inventory are still stock. Full removal procedure in
  `DEVELOPMENT/receiver/docs/wiring.md`. **This is exactly why the radio
  bring-up doc calls for the `PB3`/`PB4`/`PB5` SPI mapping** — the default
  SPI1 pins collide with these same bridged pins.
- **KiCad pin connectivity rule** (confirmed empirically, not from memory):
  a pin's `(at x y angle)` coordinate *is* its electrical connection point —
  `length`/`angle` only affect how the line is drawn, not where wires
  attach. Two pins/wire-ends at the exact same coordinate are connected
  without needing a wire between them; this is the trick used repeatedly
  (R→LED chains, the `PWR_FLAG` fix, tying the MCU's power pins into the
  existing GND/+3V3/+5V nets).
- **KiCad power-symbol nets merge by Value text, not by physical wire** —
  every `power:GND` instance (any number of them) is automatically the same
  global "GND" net just by having Value="GND"; a `PWR_FLAG`-style fix must
  still get its pin to the exact same coordinate as something already on
  that net (a T-junction via a separate wire segment silently failed ERC
  once already — see the `840725c` commit message for the full story).
- I don't have KiCad installed in this environment. Any further schematic
  changes need the same verify-by-having-the-user-run-ERC (or by scripting
  a coordinate-consistency check) loop used throughout this session — never
  trust a hand-authored `.kicad_sch` edit as correct without one or the
  other.
