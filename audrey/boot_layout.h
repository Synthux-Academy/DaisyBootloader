#pragma once

// Flash layout and boot request, shared with the Audrey-III app (its integrated
// build keeps its own copy of these numbers: src/hw/integrated/). See
// ../specs/audrey-bootloader.md.

#include <stdint.h>

#define AUDREY_APP_BASE 0x08020000u
#define AUDREY_APP_END 0x08100000u // end of bank 1
#define AUDREY_SECTOR_SIZE 0x20000u

// DFU hosts see the app region here, where a Daisy keeps its app in QSPI, because
// flash.daisy.audio only talks to devices laid out like one. Translated to
// AUDREY_APP_BASE by dfu_flash.cpp.
#define AUDREY_DFU_APP_BASE 0x90040000u

// Survives a system reset, not a power cycle. A power cycle leaves a random value:
// 1 in 2^32 to match.
#define AUDREY_BOOT_REQUEST_ADDR 0x2001FFF0u
#define AUDREY_BOOT_REQUEST_MAGIC 0xB007DF11u

// Tap (SW2) was held at power-on for AUDREY_TAP_HOLD_MS: the app flips its auto-sleep
// setting, and starts with SW2 still down. Next to the boot request word, also above
// the app's _estack (0x2001FFE0). Written by the boot flow on every start of the app
// (the magic or 0); the app reads and clears it at start-up.
#define AUDREY_TAP_FLAG_ADDR 0x2001FFF4u
#define AUDREY_TAP_FLAG_MAGIC 0x7A9F11B5u

// SW2 held this long at power-on: the app starts at once with the tap flag. Let go
// earlier (after at least SW2_MIN_TAP_MS, sw2_hold.h): bootloader mode.
#define AUDREY_TAP_HOLD_MS 5000u
