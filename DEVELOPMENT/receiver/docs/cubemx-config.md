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

For each pin below, click it on the chip diagram and pick the mode from the
dropdown, matching `wiring.md`'s pin table:

| Pin | Mode |
|---|---|
| PA0 | `TIM2_CH1` (from the Timers list, not GPIO) |
| PA1 | `ADC1_IN2` (from the Analog list) |
| PA2, PA3, PA4 | `GPIO_Input` |
| PA5–PA11 | `GPIO_Output` |
| PB0–PB4 | `GPIO_Output` |

Then, in the **System Core → GPIO** left-side config panel:
- For PA2/PA3/PA4: set **GPIO Pull-up/Pull-down** = `Pull-up`.
- For PA5–PA11 and PB0–PB4: default push-pull output is fine; set **Maximum
  output speed** = `Low` (these are just LEDs/segments, no need for fast
  slew).
- Optional but recommended: rename the pin labels (right-click each pin →
  "Enter User Label") to `START_BTN`, `CRUISE_BTN`, `KILL_BTN`,
  `SEG_A`..`SEG_G`, `BUZZER`, `LED_GREEN`, `LED_RED`, `LED_YELLOW`,
  `LED_HEARTBEAT`, `SERVO_PWM` — this makes the generated `main.h` define
  readable macros like `KILL_BTN_Pin`/`KILL_BTN_GPIO_Port` instead of raw
  `GPIO_PIN_4`, which `bench_app.c` uses.

## 3. Timer (TIM2) config — servo PWM

**Timers → TIM2**: Channel1 = `PWM Generation CH1`. In the Parameter
Settings panel:
- Prescaler and Period need to produce a 50Hz (20ms) period with enough
  resolution for pulse widths in the ~1–2ms range. With the default 80MHz
  SYSCLK (check **Clock Configuration** tab — L432 typically defaults to a
  lower HSI-derived clock unless you raise it; either is fine as long as
  prescaler/period are computed from the *actual* TIM2 clock shown there):
  pick **Prescaler = (TIM2_CLK / 1,000,000) − 1** so the timer counts in
  microseconds, and **Period (ARR) = 19999** (20,000 counts = 20ms). Then a
  servo pulse width in microseconds is just the CCR1 compare value
  (`__HAL_TIM_SET_COMPARE`) — e.g. 1500 = 1.5ms = center, matching what
  `bench_app.c` expects. `bench_app.c` computes the compare value directly
  in microseconds (`SERVO_PULSE_MIN_US`/`MAX_US`, 1000–2000), so as long as
  the prescaler/period above really do give 1 count = 1µs, no extra
  clock-matching constant is needed on the code side.

## 4. ADC1 config — throttle pot

**Analog → ADC1**: IN2 enabled (should already be set from step 2).
Resolution = 12-bit (default). Sampling time: default is fine for a
potentiometer (slow-changing signal, no need to optimize).

## 5. Clock configuration

Defaults are fine — don't need to max out SYSCLK for this bench rig. Just
note whatever TIM2 clock CubeMX shows after generation (step 3), since
that's the number the TIM2 prescaler in step 3 is computed from.

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
   `main.c`). This is what makes CubeMX emit separate `Core/Inc/adc.h` and
   `Core/Inc/tim.h` (declaring `extern ADC_HandleTypeDef hadc1;` and
   `extern TIM_HandleTypeDef htim2;`) — `bench_app.c` includes `"adc.h"`
   and `"tim.h"` directly and needs them to exist as separate files.
3. Click **GENERATE CODE** (bottom right). This produces `Core/Src/main.c`,
   `Core/Inc/main.h`, `Core/Src/adc.c` + `Core/Inc/adc.h`, `Core/Src/tim.c` +
   `Core/Inc/tim.h`, the HAL/CMSIS driver tree under `Drivers/`, the `.ioc`
   file, and a ready `.project`/`.cproject` pair — all under
   `DEVELOPMENT/receiver/firmware/`.

## 7. Open the project in STM32CubeIDE

CubeMX generating with Toolchain=STM32CubeIDE produces a project CubeIDE
can open directly — it does not need re-importing through any special
wizard. In CubeIDE: **File → Open Projects from File System...** → Import
source: `C:\GitHub\Throttle\DEVELOPMENT\receiver\firmware` → Finish. It
should appear in Project Explorer as `firmware`.

## 8. Add the shared source include paths

Right-click the project in Project Explorer → **Properties → C/C++ General
→ Paths and Symbols → Includes** (for both Debug and Release, or "All
configurations"). Add two paths:
- `${workspace_loc:/${ProjName}}/../../../../src/receiver`
- `${workspace_loc:/${ProjName}}/../../../../src/common`

(Adjust the number of `../` if your project ends up nested differently —
count directory levels from `firmware/` back up to the repo root, then down
into `src/receiver` and `src/common`.)

This lets `bench_app.c`'s `#include "receiver_firmware.c"` and the shared
`#include "throttle_protocol.h"` / `"crc8.h"` / `"battery_monitor.h"`
resolve without copying any files into the CubeIDE project.

## 9. Copy in the bench app source

`bench_app.c`/`bench_app.h` were cleared out of `firmware/` earlier to give
CubeMX/CubeIDE an empty target directory — they still exist in this repo's
git history (see the `Add receiver bench-test rig` commit). Restore them
into the freshly generated tree:
- `bench_app.c` → `Core/Src/bench_app.c`
- `bench_app.h` → `Core/Inc/bench_app.h`

CubeMX's "Generate Code" only touches its own generated files, so these two
are safe from being overwritten by future regenerations.

## 10. Wire the entry point into `main.c`

Open the generated `Core/Src/main.c`. Inside the `/* USER CODE BEGIN
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
