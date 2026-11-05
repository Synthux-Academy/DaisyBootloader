#pragma once

// Audrey integrated board support for the bootloader. Pins from Audrey-III
// src/hw/integrated/integrated_pins.h.

#include <stdint.h>
#include "daisy_core.h"

namespace audrey
{
namespace pins
{
constexpr daisy::Pin kLedData = daisy::Pin(daisy::PORTC, 6);  // TIM3_CH1
constexpr daisy::Pin kLedEn   = daisy::Pin(daisy::PORTD, 10); // VLED switch
constexpr daisy::Pin kDacXsmt = daisy::Pin(daisy::PORTD, 14); // low = muted
constexpr daisy::Pin kAmpEn   = daisy::Pin(daisy::PORTD, 11); // low = amp off
} // namespace pins

// Audio outputs quiet, clocks (400 MHz, crystal with HSI fallback), libDaisy system
// set-up with the data cache off.
void BoardInit();

// Call right after USBD_Start on the internal port: no VBUS sensing (the board has
// no VBUS pin) and HSI48 trimmed from USB start-of-frame.
void UsbInternalNoVbus();

// Poll now and then from the main loop: keeps the CRS on a working sync source.
void CrsPoll(uint32_t now_ms);

} // namespace audrey
