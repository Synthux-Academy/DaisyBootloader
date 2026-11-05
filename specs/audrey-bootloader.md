# Audrey bootloader

USB DFU bootloader for the Audrey integrated board (STM32H743VIT6). Status: **proposed, not built or
tested**. It is based on this repo's Daisy bootloader and replaces the Daisy-only `bootloader.bin`
in Audrey-III, which targets the Daisy Seed2 DFM (STM32H750 with QSPI).

Sibling project: `../_Audrey_/Audrey-III` (paths below starting with `src/` or `app.cpp` are in it).

## Requirements

| # | Requirement |
|---|---|
| R1 | Runs on the STM32H743VIT6 integrated board and programs the app into internal flash. |
| R2 | Holding **SW2** (Tap, PC3) at power-on enters the bootloader. |
| R3 | Holding **SW1** for 3 s in the running app also enters the bootloader. |
| R4 | Once entered, the bootloader stays until an upload finishes or power is removed. There is no timeout. |
| R5 | Otherwise a valid app starts with no visible delay. With no valid app, the bootloader stays active. |
| R6 | Idle: the six plate LEDs glow together, ramping up for 1 s and down for 1 s (2 s cycle). |
| R7 | Upload: the same LEDs show progress, from the `kTapPadDucking` plate (0%, erase) to the `kTapPadBodyTrack` plate (100%). |
| R8 | The bootloader can't overwrite itself, and an interrupted upload never starts as if complete. |
| R9 | Holding SW1 at power-on still starts ST's ROM DFU, as the recovery path. |

Out of scope: SD and USB-stick loading (the board has neither), audio, multi-image support, and any
change to the Audrey-III Daisy build.

## Why the stock design doesn't carry over

| Stock Daisy bootloader | Audrey integrated board | Consequence |
|---|---|---|
| Writes the app to QSPI | No QSPI, no SDRAM | New internal-flash backend |
| App runs from SRAM or QSPI | App runs from internal flash | Jump straight to flash; no copy |
| `DaisySeed` hardware init | Different pins and rails, port G not bonded | No `DaisySeed`, `QSPIHandle` or audio |
| Boot button is plain GPIO | SW1 also drives BOOT0 through Q1 | SW2 is the entry button |
| VBUS sensed | No VBUS pin on PA9 | VBUS sensing off, as `UsbInternalNoVbus` does |
| Crystal-trimmed USB path assumed | USB clock is HSI48 | CRS trim from USB SOF, as `EnableCrs` does |
| ~130 KB with FatFS and USB host | One 128 KB sector available | Remove FatFS and USB host |

## Memory map

Bank 1 of the 2 MB flash; sectors are 128 KB.

| Region | Address | Size | Contents |
|---|---|---|---|
| Sector 0 | `0x08000000` | 128 KB | Bootloader (link fails if larger) |
| Sectors 1-7 | `0x08020000` | 896 KB | App (currently ~142 KB) |
| Bank 2 | `0x08100000` | 1 MB | Unused |
| DTCM word | `0x2001FFF0` | 4 B | Boot request, above the app's `_estack` (`0x2001FFE0`) |

The boot request word survives a system reset and isn't cleared by either start-up. It follows the
same convention as `rom_request`: a power cycle leaves a random value, with a 1 in 2^32 chance of a
false match.

The bootloader's `_estack` must also be `0x2001FFE0`. The stock `0x20020000` would put the stack on
top of the boot request word, and it breaks the ROM loader's stack limit noted in `core.lds`.

## Boot flow

This runs from flash, before clocks, RAM copies or peripherals:

```text
Reset_Handler
  USB or CRS clock enabled (ROM loader jumped here)? -> system reset
  request = boot request word; clear the word
  configure PC3 as input, wait about 10 ms, read SW2
  if request valid, or SW2 low, or app invalid -> bootloader mode
  otherwise -> restore GPIOC to its reset state, then jump to the app
```

- **USB clock check:** this is the check from `src/hw/core/startup_core.c`. It is needed because
  flashing the bootloader itself through ROM DFU with `:leave` jumps here with USB still running.
- **App valid:** the initial SP is in `0x20000000-0x20020000`, and the reset vector is odd and in
  `0x08020000-0x08100000`.
- **Jump:** interrupts off, set `VTOR` and `MSP`, branch. The app's own USB clock check passes
  because USB was never started on this path.

**Bootloader mode** copies code to RAM, starts clocks at 400 MHz (no boost), then starts LEDs and
USB. Clock start-up uses the crystal check and HSI fallback from `integrated_seed.cpp`, so a failed
crystal doesn't leave the bootloader dead. It also drives `AMP_EN` and `XSMT` low.

## Running from RAM

Code that fetches from bank 1 stalls while bank 1 is erased or programmed, which would stop USB and
the LEDs. So `.isr_vector`, `.text` and `.rodata` are linked to AXI SRAM, loaded from flash, and
`VTOR` points to the RAM copy. Only the boot flow above and the copy loop execute from flash.

Sector erases use `HAL_FLASHEx_Erase_IT`, so the main loop keeps running and remains the only place
that draws LEDs. Flash-word writes are short enough to be blocking.

## DFU backend

Starts from `shared/dfu.cpp`, using the internal FS port (PA11/PA12).

**Memory layout string.** Only the app region is exposed, split into virtual 4 KB pages, because
segment counts are limited to two digits:

```text
@Flash /0x08020000/99*4Kg,99*4Kg,26*4Kg
```

**Erase.**
- Reject any address outside `0x08020000-0x08100000`. Mass erase isn't supported; the middleware
  already rejects it.
- Each distinct virtual page requested counts toward the expected total (`expected = pages x 4 KB`).
  This rounds the image size up by less than 4 KB, so the bar is forced to full at commit.
- The first request in a sector sets that sector's bit in a pending mask. The main loop erases
  pending sectors one at a time. A request for a sector already pending or erased does nothing else.
- The queue is needed because stock `dfu.cpp` returns an error if a request arrives while busy, and
  a host can send the next request before a 128 KB erase finishes.

**Write.**
- Program 32-byte flash words; pad a short final chunk with `0xFF`.
- Hold back the first 32 bytes of the app in RAM (see Commit).
- Read back and compare each chunk.

**Status (`bwPollTimeout`).** `MEM_If_GetStatus_FS` runs *before* the request it reports on, and the
`Add` it receives isn't the erase address. So timeouts depend on queue state:
- Erase requests report a short timeout.
- All other requests, including `SET_ADDRESS`, report the worst-case time for the erases still
  pending, or about 3 ms if none are. `dfu-util` sends `SET_ADDRESS` before every chunk, so it sleeps
  through the erases before its first write. The middleware can't report busy again on the next poll,
  so this figure has to be a true worst case. A small upload may therefore sit on plate 6 for several
  seconds longer than the erase takes.
- After a failed write or verify, report `errVERIFY` so the host stops with an error. Stock code
  always reports OK.

**Commit.** The app's first 32 bytes, including its SP and reset vector, are written last.
1. Build with `USBD_DFU_VENDOR_EXIT_ENABLED=1`, so `DFU_Leave` calls our `LeaveDFU` instead of resetting.
2. `LeaveDFU` runs in the USB interrupt, so it only sets a flag.
3. The main loop finishes queued work, writes the held 32 bytes, then resets.

`dfu-util` sends `DFU_ABORT` between the last write and the leave request, and `DFU_Leave` is reached
from the resulting `dfuIDLE` state. An abort must therefore keep the held bytes and upload state. Only
the next erase starts a new upload and discards them.

The reset then starts the app through the normal boot flow. An upload that never reaches commit
leaves no valid vectors, so the next boot stays in the bootloader. **This means uploads must use
`:leave`.** A stock `enable_jump()` path is not used: nothing in the middleware calls it.

**Read.** Replace the QSPI code, which also has a bug (it offsets by `*src`), with reads of the app
region. The stored image won't match the file until commit.

## LEDs

The `Ws2812` driver is copied from `src/hw/ws2812.{h,cpp}` with a note of its origin. Its only change
is owning its TIM3 handle instead of using `TimerHandle` (see libDaisy below).

| Setting | Value |
|---|---|
| Data | PC6, TIM3 CH1 |
| Power | `LED_EN` PD10 high |
| Frames | 40 LEDs, all sent, at about 50 Hz |
| Cap | `SetBrightnessLimit(0.8)`, the integrated board's value |

All 40 are sent so the ring (indexes 8-39) goes dark. WS2812s hold their last colour, so the app's
ring would otherwise stay lit after a reset into the bootloader.

Plates in progress order (`kPadLed` in `src/ui/core.ui.h`):

| Plate constant | Plate | Chain index |
|---|---|---|
| `kTapPadDucking` | 6 | 6 |
| `kTapPadSensitivity` | 5 | 5 |
| `kTapPadAutoPump` | 4 | 4 |
| `kTapPadScale` | 3 | 3 |
| `kTapPadDrone` | 2 | 1 |
| `kTapPadBodyTrack` | 1 | 0 |

Colour is white and base brightness is 0.5, both as in the old binary. Chain indexes 2 and 7 stay dark.

| State | Trigger | Display |
|---|---|---|
| Idle | Bootloader mode, no upload activity | All six: `0.5 x level`, where `level` ramps 0→1→0 over 2 s |
| Erase | First erase until first write | Plate 6 only, steady |
| Write | First write until commit | `p = written / expected`. Plate 6 stays lit and the other five fill in order, each fading in over its 1/5 of `p`, so all six are lit at `p = 1` |
| Stalled | No DFU activity for 5 s during an upload | Back to idle; the image stays uncommitted |
| Failed | Write or verify error | All six red, blinking at 1 Hz, until the next upload starts or power is removed |

## App changes (Audrey-III, integrated build only)

| File | Change |
|---|---|
| `src/hw/core/core.lds` | `FLASH` origin `0x08020000`, length 896 KB |
| `src/hw/core/startup_core.c` | Set `VTOR` to the app's vector table after `SystemInit()`, which otherwise sets it to `0x08000000` |
| `Makefile` | `FLASH_ADDRESS = 0x08020000` before the libDaisy include; add a bootloader flash target |
| `integrated_seed.{h,cpp}` | Add `EnterBootloader()`: mute outputs, LEDs off, wait for SW1 release, write the boot request, reset |
| `app.cpp` | SW1 3 s calls `EnterBootloader()` instead of `EnterUpdateMode()` |
| `README.md`, `AUDREY-IV.md`, `specs/` | Update flash instructions |

`EnterBootloader()` must wait for release because resetting with SW1 held starts ROM DFU. `rom_request`
and `EnterUpdateMode()` can go once nothing uses them.

The linker comment about the ROM loader's stack limit still applies, so keep `_estack` as it is.

## libDaisy

Keep this repo's stock libDaisy submodule (`24be4afe`, upstream `v8.1.0-10`). The bootloader doesn't
need `bleeptools/libDaisy`, and Audrey-III stays on the fork unchanged. The two are separate binaries
and share only the flash layout and the boot request word.

The bootloader's own driver copy replaces the one fork API it needs, `TimerHandle::GetHALHandle()`:
it owns a `TIM_HandleTypeDef` for TIM3 and sets it up with the same HAL calls and values as
`TimerHandle::Init`. Compiling `ws2812.cpp` against stock headers with `FILEIO_ENABLE_FATFS_READER`
unset reports only that API plus two `WavPlayer.h` errors caused by the missing define. The stock
Makefile sets that define, but a compile with it set has not been run yet.

Stock also already defines `USBD_SUPPORT_USER_STRING_DESC`, which DFU needs to serve the memory layout
string. The fork misspells it and would have needed upstream's fix `7651b28c`. Both repos pin the same
HAL, CMSIS device and USB middleware commits, and the stock submodules need
`git submodule update --init --recursive` before the first build.

`libdaisy.a` stays built for STM32H750 (same die and register map), as the app's does today, with
`-DSTM32H743xx` for the bootloader's own files. The stock `bootloader/` target keeps building.

## Files

```text
audrey/
  Makefile           BOARD target, -DSTM32H743xx, USBD_DFU_VENDOR_EXIT_ENABLED=1, no FatFS
  boot_linker.lds    128 KB flash, code in AXI SRAM, .sram1_bss for the LED DMA buffer
  startup.c          Vectors and the boot flow above
  main.cpp           Clocks, USB, main loop
  dfu_flash.cpp/.h   DFU backend
  led_status.cpp/.h  LED states
  board.cpp/.h       Pins, clock fallback, no-VBUS USB and CRS, copied from Audrey-III
  ws2812.cpp/.h      Copied driver, with its own TIM3 handle
shared/usbd_desc.c   Reused as it is (Synthux / Audrey III)
```

## Flashing

```sh
# First time, or recovery: hold SW1 at power-on (ROM DFU)
dfu-util -a 0 -s 0x08000000:leave -D build/audrey_bootloader.bin -d ,0483:df11

# App: bootloader mode (SW2 at power-on, or SW1 for 3 s in the app)
dfu-util -a 0 -s 0x08020000:leave -D build-integrated-nodebug/Audrey-Integrated.bin -d ,0483:df11
```

The ROM loader and this bootloader share USB ID `0483:df11`; the product string tells them apart.
Existing boards with the app at `0x08000000` need both steps once. Writing to `0x08000000` in
bootloader mode is rejected.

## Acceptance tests

Run on pilot board 1.

| # | Test | Expected |
|---|---|---|
| T1 | Blank app region, power on | Bootloader mode, idle glow |
| T2 | Valid app, power on | App starts, with no glow |
| T3 | SW2 held at power-on | Idle glow; still running after 10 minutes |
| T4 | SW1 held 3 s in app, then released | Idle glow after release |
| T5 | SW1 held at power-on | ROM DFU appears |
| T6 | Upload the app with `dfu-util` 0.11 | Plate 6 during erase, smooth fill to all six, app starts |
| T7 | Read the app region back after an upload | Matches the file |
| T8 | Write to `0x08000000` in bootloader mode | Rejected; bootloader still boots |
| T9 | Unplug USB mid-erase and mid-write, then power on | Bootloader mode, and a retry succeeds |
| T10 | Stop the host mid-upload | Idle glow after 5 s, and a retry succeeds |
| T11 | Upload without `:leave`, then power cycle | Bootloader mode |
| T12 | Upload an image over 512 KB (3+ sector erases queued) | No USB stall or `dfu-util` error |
| T13 | Enter from a running app with the ring lit | Ring goes dark |
| T14 | Build bootloader | Under 128 KB |

## To verify during implementation

Confirmed by reading the `dfu-util` 0.11 source (`dfuse.c`, `dfuse_mem.c`) and the fork's `usbd_dfu.c`,
but not yet run on hardware:

- `dfu-util` erases every page before its first write, so `expected` is known before writing starts.
  Other DFU hosts may differ, and only `dfu-util` is in scope.
- It parses the comma-separated layout string. Its parser has no two-digit limit, so the limit only
  matters for other hosts.
- `:leave` sends abort, then a zero-length download, which reaches `LeaveDFU`.

Still open:

- **Worst-case erase time.** The design assumes about 3 s per 128 KB sector, from memory. Check it
  against DS12110, which couldn't be downloaded for this review, and measure it on the board.
- **No reset button.** The docs show none, but the integrated board's schematic wasn't available for
  this review.

## Choices to confirm

- **Plate 6 lit at 0%:** the old binary showed nothing during erase. This follows your "0% & erase".
- **Failed state:** the red blink is new, since the old binary showed nothing for errors.
- **5 s stall timeout:** returning to idle during an upload is also new.
