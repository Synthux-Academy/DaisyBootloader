// USB DFU bootloader for the Audrey integrated board. See
// ../specs/audrey-bootloader.md. The choice between the app and this code was made
// in startup.c: reaching main means we stay here until an upload commits or the
// power goes.

#include "board.h"
#include "daisy.h"
#include "dfu_flash.h"
#include "led_status.h"

using namespace daisy;

extern "C" void* g_pfnVectors[];

int main(void)
{
    // SystemInit pointed it at flash
    SCB->VTOR = reinterpret_cast<uint32_t>(g_pfnVectors);
    __DSB();
    __ISB();

    audrey::BoardInit();
    audrey::LedStatusInit();
    const bool usb_ok = audrey::DfuInit();

    while(true)
    {
        const uint32_t now = System::GetNow();
        audrey::DfuProcess();
        audrey::CrsPoll(now);
        audrey::LedStatusUpdate(now,
                                usb_ok ? audrey::DfuGetPhase() : audrey::DfuPhase::Failed,
                                audrey::DfuGetProgress());
    }
}
