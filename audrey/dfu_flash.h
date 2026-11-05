#pragma once

// USB DFU onto internal flash, for the app region only. See
// ../specs/audrey-bootloader.md, "DFU backend".

namespace audrey
{
enum class DfuPhase
{
    Idle,   // no upload running
    Erase,  // erase requests so far, no data yet
    Write,  // data arriving
    Failed, // erase, write or verify error, until the next upload
};

// Starts USB. Returns false if the USB stack refused.
bool DfuInit();

// Main loop: performs the flash work the USB interrupt queued. Doesn't return if an
// upload just committed: it resets to start the app.
void DfuProcess();

DfuPhase DfuGetPhase();

// 0..1, written / expected, for DfuPhase::Write
float DfuGetProgress();
} // namespace audrey
