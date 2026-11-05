#pragma once

#include <stdint.h>
#include "dfu_flash.h"

namespace audrey
{
// Blanks the whole chain and powers the LEDs. Blocks until the LED rail is up.
void LedStatusInit();

// Blanks the chain and removes LED power, ready for a reset. Blocks briefly.
void LedStatusOff();

// Call from the main loop; sends a new frame every few milliseconds.
void LedStatusUpdate(uint32_t now_ms, DfuPhase phase, float progress);
} // namespace audrey
