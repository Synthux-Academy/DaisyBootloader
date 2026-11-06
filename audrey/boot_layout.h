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
