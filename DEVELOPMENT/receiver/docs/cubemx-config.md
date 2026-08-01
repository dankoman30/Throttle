# CubeMX / CubeIDE project setup — receiver bench rig

You have STM32CubeIDE 2.2.0 installed and an empty workspace. This walks
through creating the project and wiring in `bench_app.c`. Written for someone
coming from Arduino — CubeMX's job is just "pick a pin, pick a mode," it's
not doing anything conceptually different from `pinMode()`.

## 1. Create the project

This install's menu wording differs from older STM32CubeIDE tutorials/docs
you may find online — confirmed against the release notes and user guide
(UM2553, UM2609) bundled with this exact install
(`C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.documentation_*\docs\`).

1. **File → STM32 Project Create/Import → Create New STM32 Project**.
2. A popup offers 4 tiles: "STM32CubeIDE empty project", "C Project", "C++
   Project", "STM32 CMake project". Pick **STM32CubeIDE empty project** —
   despite the name, this is the STM32Cube/HAL-integrated one with MCU/board
   selection and the CubeMX peripheral-configuration tool (the plain "C
   Project"/"C++ Project" tiles are generic Eclipse CDT with no STM32
   integration at all; "STM32 CMake project" does the same MCU/board
   selection but builds via CMake instead of CubeIDE's default managed
   makefile — not what these docs assume).
3. **Board Selector** tab (not "MCU/MPU Selector" — picking the board
   pre-selects the right pins for the ST-Link, USB, and the on-board LED so
   you don't have to know them) → search `NUCLEO-L432KC` → select it → Next.
4. Project name: `bench_app` (or similar). **Location**: uncheck "use
   default location" and point it at
   `C:\GitHub\Throttle\DEVELOPMENT\receiver\firmware` directly, so the
   generated project lands in the repo instead of the CubeIDE workspace
   folder. **The target directory must be completely empty** — the wizard
   refuses a non-empty one. If `bench_app.c`/`bench_app.h` are already
   sitting in there from a previous step, move them out temporarily, create
   the project into the now-empty folder, then move them into the
   generated `Core/Src`/`Core/Inc` afterward (step 8 below).
5. Targeted Language: **C**. Targeted Binary Type: leave the preselected
   **Executable** (not "Static library" — that produces a `.a` meant to be
   linked into another project, not a standalone flashable image). Finish.
   Say **Yes** if it asks to initialize all peripherals with their default
   mode (harmless, we'll override what we need) and **Yes** to open the
   Device Configuration Tool (the `.ioc` pinout view).

## 2. Pin configuration (Device Configuration Tool → Pinout & Configuration)

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
  `bench_app.c` expects.
- Note the actual TIM2 clock CubeMX reports after setting this, since
  `bench_app.c` has a `SERVO_TIM_CLK_HZ` constant at the top you'll need to
  match to it.

## 4. ADC1 config — throttle pot

**Analog → ADC1**: IN2 enabled (should already be set from step 2).
Resolution = 12-bit (default). Sampling time: default is fine for a
potentiometer (slow-changing signal, no need to optimize).

## 5. Clock configuration

Defaults are fine — don't need to max out SYSCLK for this bench rig. Just
note whatever TIM2 clock CubeMX shows after generation (step 3).

## 6. Generate code

**Project → Generate Code** (or the gear-with-gears toolbar icon). This
creates `Core/Src/main.c`, `Core/Inc/main.h`, the HAL/CMSIS driver tree, and
the `.ioc` file, all under
`DEVELOPMENT/receiver/firmware/`.

## 7. Add the shared source include paths

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

## 8. Copy in the bench app source

Copy the committed `Core/Src/bench_app.c` and `Core/Inc/bench_app.h` from
this repo's `DEVELOPMENT/receiver/firmware/` into the same paths inside the
CubeIDE-generated project (they may already be there if you generated
directly into this folder — just don't let CubeMX's "Generate Code" step
overwrite them; it only touches its own generated files, not new ones you
add).

## 9. Wire the entry point into `main.c`

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

## 10. Build and flash

Hammer icon to build. Then **Run → Debug** (or the bug icon) — CubeIDE talks
to the on-board ST-Link automatically over the same USB connection; you
don't need to do anything with the `D:\` mass-storage drive for this (that's
a separate drag-and-drop flashing mode some Nucleos support — CubeIDE's
debugger flashing is more convenient here and lets you set breakpoints).
