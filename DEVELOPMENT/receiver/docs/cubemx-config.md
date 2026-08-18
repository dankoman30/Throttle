# CubeMX / CubeIDE project setup — receiver bench rig

You have STM32CubeIDE 2.2.0 and (separately) STM32CubeMX 6.18.1 installed,
plus an empty CubeIDE workspace. This walks through configuring the board in
CubeMX, generating a CubeIDE-ready project from it, and wiring in
`bench_app.c`. Written for someone coming from Arduino — CubeMX's job is
just "pick a pin, pick a mode," it's not doing anything conceptually
different from `pinMode()`.

**Why two separate tools:** newer STM32CubeIDE releases (this install
included) dropped the built-in CubeMX/Device Configuration Tool integration
— confirmed from this install's own bundled release notes (UM2609/UM2553,
`C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.documentation_*\docs\`)
and from a link inside CubeIDE's own Information Center pointing at the
standalone STM32CubeMX download. So: CubeMX (standalone) does the board/pin/
peripheral configuration and generates the project; CubeIDE just opens what
it generated. (STM32CubeMX2, the newer tool ST also lists for download, is
scoped to the STM32C5 series only as of v1.1.1 — irrelevant here, the L432KC
needs plain STM32CubeMX.)

## 1. Configure the board in STM32CubeMX

1. Launch STM32CubeMX → **File → New Project** (or the "ACCESS TO BOARD
   SELECTOR" tile on the home screen).
2. **Board Selector** tab (not "MCU/MPU Selector" — picking the board
   pre-selects the right pins for the ST-Link, USB, and the on-board LED so
   you don't have to know them) → search `NUCLEO-L432KC` → select it →
   Next/OK.
3. If asked to initialize all peripherals with their default mode, **Yes**
   is fine (harmless, we override what we need below).

## 2. Pin configuration (Pinout & Configuration tab)

**Do not touch PA0, PA2, PA13, PA14, PA15, or PB3** — all locked/reserved by
this board (ST-Link SWD, VCP UART, on-board LED, MCO clock-in — see
`wiring.md`). Leave them at whatever CubeMX defaults to. Also note **PB2
doesn't exist** on this package — don't look for it in the Pinout view.

Click each pin on the chip diagram, pick its mode from the dropdown that
pops up, then look at the **configuration panel** below/beside the diagram
(clicking a pin switches that panel's context to it) — it has the mode
dropdowns plus a **User Label** text field, all for that one pin. Set every
column below for each pin (`—` means that field doesn't apply/isn't shown
for that pin's mode). `bench_app.c` refers to pins by these labels
(`KILL_BTN_Pin`/`KILL_BTN_GPIO_Port`, etc.), not raw pin numbers, so the
label text must match exactly (case-sensitive):

| Pin | Mode | Pull-up/Pull-down | Max output speed | GPIO output level | User Label |
|---|---|---|---|---|---|
| PA1 | `ADC1_IN6` (Analog list) | — | — | — | *(none — code uses the ADC handle)* |
| PA3 | `GPIO_Input` | `Pull-up` | — | — | `START_BTN` |
| PA4 | `GPIO_Input` | `Pull-up` | — | — | `CRUISE_BTN` |
| PA5 | `TIM2_CH1` (Timers list, not GPIO) | — | — | — | *(none — code uses the timer handle)* |
| PA6 | `GPIO_Input` | `Pull-up` | — | — | `KILL_BTN` |
| PA7 | `GPIO_Output` | — | `Low` | `Low` | `SEG_A` |
| PA8 | `GPIO_Output` | — | `Low` | `Low` | `SEG_B` |
| PA9 | `GPIO_Output` | — | `Low` | `Low` | `SEG_C` |
| PA10 | `GPIO_Output` | — | `Low` | `Low` | `SEG_D` |
| PA11 | `GPIO_Output` | — | `Low` | `Low` | `SEG_E` |
| PA12 | `GPIO_Output` | — | `Low` | `Low` | `SEG_F` |
| PB0 | `GPIO_Output` | — | `Low` | `Low` | `SEG_G` |
| PB1 | `GPIO_Output` | — | `Low` | `Low` | `BUZZER` |
| PB4 | `GPIO_Output` | — | `Low` | `Low` | `LED_GREEN` |
| PB5 | `GPIO_Output` | — | `Low` | `Low` | `LED_RED` |
| PB6 | `GPIO_Output` | — | `Low` | `Low` | `LED_YELLOW` |
| PB7 | `GPIO_Output` | — | `Low` | `Low` | `LED_HEARTBEAT` |

Notes on the output columns: **Max output speed** = `Low` is plenty for
LEDs/segments (no fast switching needed) — leave **GPIO mode** at its
default `Output Push Pull` (don't switch to Open Drain). **GPIO output
level** = `Low` just sets the pin's state for the brief window between
reset and `bench_app_init()` running — `bench_app_init()` immediately
drives the buzzer/LEDs off and the display to `0` anyway, so this default
doesn't have to be exact, `Low` just avoids anything flashing on briefly at
power-up.

## 3. Timer (TIM2) config — servo PWM

**Before this section**, check the **Clock Configuration** tab and find the
**APB1 timer clocks (MHz)** box in the clock tree (this is TIM2's clock
source) — on a freshly-generated NUCLEO-L432KC project this is **32 MHz**.
If you changed the clock config, re-derive the Prescaler below from
whatever that box actually shows: **Prescaler = (APB1 timer clock in Hz /
1,000,000) − 1**, so the timer counts in whole microseconds. At 32MHz that's
`32,000,000 / 1,000,000 − 1 = 31`.

**Timers → TIM2**, Channel1 = `PWM Generation CH1`. In the **Parameter
Settings** panel (top-level, not the PWM sub-section yet):
- **Prescaler**: `31`
- **Counter Mode**: `Up` (default, leave as-is)
- **Counter Period (Auto-reload register)**: `19999` — change this from the
  default `4294967295`; this is the field that actually sets the 20ms (50Hz)
  period (20,000 counts × 1µs).

Then scroll to the **PWM Generation Channel 1** sub-section (a separate
block further down, not the same as the settings above):
- **Pulse**: `1500` (1.5ms = servo center — just a sane boot-time default;
  `bench_app.c` overwrites the compare value every tick via
  `__HAL_TIM_SET_COMPARE`, computing it directly in microseconds via
  `SERVO_PULSE_MIN_US`/`MAX_US`, 1000–2000, so this only matters for the
  brief window before `bench_app_init()` runs)
- **CH Polarity**: `High` (default, leave as-is)

Everything else on this screen (One Pulse Mode, Auto-reload preload,
trigger/slave-mode settings) — leave at default, none of it matters for a
plain continuous PWM output like this.

## 4. ADC1 config — throttle pot

**Analog → ADC1** → in the "ADC1 Mode and Configuration" panel, find the
**IN6** channel dropdown and set it to **"IN6 Single-ended"** (not
"Differential" — that's for measuring a voltage difference between two
channel pairs, not a plain potentiometer wiper against GND; "Disable" is
just the unset default). Selecting `ADC1_IN6` as PA1's mode back in step 2
only routes the pin to the ADC peripheral — this channel-mode dropdown is a
separate, explicit setting.

Resolution = 12-bit (default). Sampling time: default is fine for a
potentiometer (slow-changing signal, no need to optimize).

## 5. Clock configuration

Defaults are fine — don't need to max out SYSCLK for this bench rig. On a
freshly board-selected NUCLEO-L432KC project with default peripherals
initialized, SYSCLK/AHB/APB1/APB2 all come up at **32MHz** (all prescalers
`/1`), which makes **APB1 Timer clocks = 32MHz** — the number step 3's TIM2
Prescaler (`31`) is computed from. If yours differs, re-derive that
Prescaler value before moving on.

**You'll likely hit a clock warning around here** — either when opening
this tab for the first time after enabling ADC1, or when clicking
**GENERATE CODE** in step 6: *"These peripherals still have some not
configured or wrong parameter values: [Clock]"*, and/or a popup asking *"Do
you want to run automatic clock issues solver?"*. This is an invalid
multiplier left over in the **PLLSAI1** branch (feeds the ADC/SAI1/I2C3
clock muxes — shown as a magenta-highlighted field in that part of the
tree), not the main SYSCLK→AHB→APB1/APB2 chain TIM2 depends on. Click
**Yes** to run the automatic solver, then confirm **APB1 Timer clocks
(MHz)** still reads the same value it did before (32, unless you changed
something) — if it didn't move, the fix only touched the ADC-clock branch
and step 3's Prescaler is still valid.

## 6. Project Manager view → Generate Code

The Project Manager view has 3 tabs: **Project**, **Code Generation**,
**Advanced Settings**.

1. **Project** tab: **Project Name**: `firmware`. **Project Location**:
   point at `C:\GitHub\Throttle\DEVELOPMENT\receiver` (CubeMX appends the
   project name as a subfolder of Location, so `Location=...\receiver` +
   `Name=firmware` generates into `...\receiver\firmware\`, which is where
   this repo's `bench_app.c`/`bench_app.h` and the rest of this doc expect
   the project to live). **Application structure**: leave `Basic` (this
   project uses no middleware). **Toolchain/IDE**: `STM32CubeIDE`. Once
   that's selected, an extra **"Generate under root"** checkbox appears —
   **check it**, so the `.project`/`.cproject` land directly in
   `firmware\` instead of a nested toolchain subfolder (unchecked, CubeMX
   puts them under a separate `STM32CubeIDE\` subfolder, which would break
   the relative include paths in step 8 below and the location this doc
   assumes throughout).
2. **Code Generation** tab: check **"Generate peripheral initialization as
   a pair of .c/.h files"** (the alternative is folding everything into
   `main.c`). This is what makes CubeMX emit separate `Inc/adc.h` and
   `Inc/tim.h` (declaring `extern ADC_HandleTypeDef hadc1;` and
   `extern TIM_HandleTypeDef htim2;`) — `bench_app.c` includes `"adc.h"`
   and `"tim.h"` directly and needs them to exist as separate files.
3. Click **GENERATE CODE** (bottom right). This produces `Src/main.c`,
   `Inc/main.h`, `Src/adc.c` + `Inc/adc.h`, `Src/tim.c` + `Inc/tim.h`, the
   HAL/CMSIS driver tree under `Drivers/`, the `.ioc` file, and a ready
   `.project`/`.cproject` pair — all under `DEVELOPMENT/receiver/firmware/`.
   **Note:** with Application structure = `Basic`, CubeMX generates flat
   `Src/`/`Inc/` folders at the project root — *not* the `Core/Src`/
   `Core/Inc` layout some CubeIDE tutorials show (that's what `Advanced`
   structure or CubeIDE's own native project wizard produce instead). This
   doc's paths below assume the flat `Src/`/`Inc/` layout.

## 7. Open the project in STM32CubeIDE

CubeMX generating with Toolchain=STM32CubeIDE produces a project CubeIDE
can open directly — it does not need re-importing through any special
wizard. In CubeIDE: **File → Open Projects from File System...** → Import
source: `C:\GitHub\Throttle\DEVELOPMENT\receiver\firmware` → Finish. It
should appear in Project Explorer as `firmware`.

## 8. Add the shared source include paths

Right-click the project in Project Explorer → **Properties → C/C++ General
→ Paths and Symbols → Includes**, with **Configuration: [All
configurations]** selected at the top (so the change applies to both Debug
and Release in one shot) and **"GNU C"** selected in the left-hand language
tree (this list is per-language — adding while a different language, e.g.
Assembler, is selected puts it somewhere `bench_app.c`'s C compile never
sees; check **"Add to all languages"** in the Add dialog to sidestep this).

The path from `firmware/` (the project root) back up to the repo root is
exactly **3** levels (`firmware` → `receiver` → `DEVELOPMENT` →
`Throttle`), not 4 — verify with `cd ../../../` from `firmware/` before
trusting any different depth. Add two paths:
- `${workspace_loc:/${ProjName}}/../../../src/receiver`
- `${workspace_loc:/${ProjName}}/../../../src/common`

**Type or paste this directly into the "Directory:" field** — do not use
the "Workspace..." browse button (`src/receiver`/`src/common` are outside
this project, so they won't appear there) or the "File system..." button
if you want the portable `${workspace_loc:...}` form (File system... picks
an absolute path instead, e.g. `C:\GitHub\Throttle\src\receiver`, which
works too and is a reasonable fallback if the variable form gives trouble,
but is tied to this one machine).

**This exact combination of GUI actions is known to silently corrupt the
value** — pasting into the Directory field while "Is a workspace path" is
unchecked has been observed to prepend a stray `../Debug/../` (or just
`../`) before the `${workspace_loc:...}` variable, producing something
like `"../Debug/../${workspace_loc:/${ProjName}}/.../src/receiver"` that
looks plausible in the UI but doesn't resolve correctly (you can't mix a
literal relative prefix with a variable that resolves to an absolute path
in one string). **After clicking Apply, verify what actually got written**
by opening `firmware/.cproject` in a text editor and searching for
`src/receiver` — the saved value should read exactly
`&quot;${workspace_loc:/${ProjName}}/../../../src/receiver&quot;`, nothing
extra before the `${`. If it doesn't match, delete the entry in the GUI and
re-add it, or edit `.cproject` directly (with CubeIDE closed, to avoid it
overwriting your edit on next save) — search for both
`tool.c.compiler.option.includepaths` blocks (Debug and Release each have
their own) and fix the `listOptionValue` entries there.

This lets `bench_app.c`'s `#include "receiver_firmware.c"` and the shared
`#include "throttle_protocol.h"` / `"crc8.h"`
resolve without copying any files into the CubeIDE project.

## 9. Copy in the bench app source

*(Already done as of this doc's last update — `Src/bench_app.c` and
`Inc/bench_app.h` are committed in this repo alongside the generated
project. This step is here for reference/regeneration only.)*

`bench_app.c`/`bench_app.h` live in this repo's git history if they're ever
missing from a fresh checkout (see the `Add receiver bench-test rig`
commit) — restore them into the generated tree:
- `bench_app.c` → `Src/bench_app.c`
- `bench_app.h` → `Inc/bench_app.h`

CubeMX's "Generate Code" only touches its own generated files, so these two
are safe from being overwritten by future regenerations.

## 10. Wire the entry point into `main.c`

*(Also already done — noted here for reference/regeneration only.)*

Open the generated `Src/main.c`. Inside the `/* USER CODE BEGIN
Includes */` block add:
```c
#include "bench_app.h"
```
Inside `/* USER CODE BEGIN 2 */` (after all the `MX_*_Init()` calls, before
the `while (1)`) add:
```c
bench_app_init();
```
Inside the `while (1)` loop, in its `/* USER CODE BEGIN 3 */` block, add:
```c
bench_app_tick();
```
These markers are preserved across future CubeMX regenerations (e.g. if you
add a peripheral later), so your edits won't be lost.

## 11. Build and flash

Hammer icon to build. Then **Run → Debug** (or the bug icon) — CubeIDE talks
to the on-board ST-Link automatically over the same USB connection; you
don't need to do anything with the `D:\` mass-storage drive for this (that's
a separate drag-and-drop flashing mode some Nucleos support — CubeIDE's
debugger flashing is more convenient here and lets you set breakpoints).
