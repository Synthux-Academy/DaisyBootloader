#include "dfu_flash.h"

#include <cstring>

#include "boot_layout.h"
#include "board.h"
#include "daisy.h"
#include "led_status.h"
#include "usbd_ctlreq.h"
#include "usbd_def.h"
#include "usbd_desc.h"
#include "usbd_dfu.h"
#include "usbd_dfu_if.h"
#include "usbd_ioreq.h"

extern "C"
{
    extern USBD_HandleTypeDef hUsbDeviceFS;
    extern PCD_HandleTypeDef  hpcd_USB_OTG_FS;
}

namespace
{
constexpr uint32_t kFlashWord   = 32; // bytes, the smallest programmable unit
constexpr uint32_t kVirtualPage = 4096;
constexpr uint32_t kAppSize     = AUDREY_APP_END - AUDREY_APP_BASE;
constexpr uint32_t kNumPages    = kAppSize / kVirtualPage;
constexpr uint32_t kNumSectors  = kAppSize / AUDREY_SECTOR_SIZE;
// App sector 0 is flash sector 1: sector 0 holds the bootloader
constexpr uint32_t kFirstFlashSector = 1;

// bwPollTimeout. These only pace the host: it can't move on while DfuSetup reports
// busy, so none of them has to cover the flash work itself.
constexpr uint32_t kRequestPollMs   = 1;  // the request has only been queued
constexpr uint32_t kEraseBusyPollMs = 50; // a sector takes about 1 s
constexpr uint32_t kWriteBusyPollMs = 1;  // a 1 KB chunk takes about 4 ms

constexpr uint32_t kStallMs = 5000;

constexpr uint8_t kStatusOk     = 0x00;
constexpr uint8_t kStatusVerify = 0x07; // errVERIFY
constexpr uint8_t kStateDnBusy  = 0x04; // dfuDNBUSY

constexpr uint16_t kMediaOk   = USBD_OK;
constexpr uint16_t kMediaFail = USBD_FAIL;

// Virtual 4 KB pages, so erase requests give the image size to within 4 KB although
// real sectors are 128 KB. Counts are kept to two digits for DfuSe hosts.
#define AUDREY_FLASH_LAYOUT "@Flash /0x08020000/99*4Kg,99*4Kg,26*4Kg"
static_assert(99 + 99 + 26 == kNumPages, "layout string doesn't match the app region");

// Written by the USB interrupt, read and cleared by the main loop
volatile uint32_t pending_sectors = 0; // erase queued or running
volatile uint32_t erased_sectors  = 0;
volatile bool     write_pending   = false;
volatile uint32_t write_addr      = 0;
volatile uint32_t write_len       = 0;
volatile bool     leave_requested = false;

volatile bool     upload_active = false;
volatile bool     data_seen     = false;
volatile bool     failed        = false;
volatile uint32_t expected      = 0;
volatile uint32_t written       = 0;
volatile uint32_t deadline_ms   = 0; // stall limit, while an upload is active

uint32_t page_bits[(kNumPages + 31) / 32];

// Main loop only
bool     erase_running = false;
uint32_t erase_sector  = 0;

// The app's first flash word holds its SP and reset vector, and is written last:
// an image that never reaches commit has no valid vectors.
bool held_valid = false;
__attribute__((aligned(4))) uint8_t held_word[kFlashWord];

// Largest chunk, rounded up to whole flash words
__attribute__((aligned(4))) uint8_t chunk[USBD_DFU_XFER_SIZE + kFlashWord];

bool InAppRegion(uint32_t addr, uint32_t len)
{
    return addr >= AUDREY_APP_BASE && addr < AUDREY_APP_END
           && len <= AUDREY_APP_END - addr;
}

uint32_t SectorOf(uint32_t addr)
{
    return (addr - AUDREY_APP_BASE) / AUDREY_SECTOR_SIZE;
}

// The USB interrupt also changes these masks
void SectorDone(uint32_t bit, bool ok)
{
    __disable_irq();
    pending_sectors = pending_sectors & ~bit;
    if(ok)
        erased_sectors = erased_sectors | bit;
    __enable_irq();
}

void Fail()
{
    failed        = true;
    upload_active = false;
}

// Flash by register rather than through the HAL: libdaisy.a is built for STM32H750,
// whose HAL rejects addresses above its 128 KB of flash. FLASH_CR_PSIZE_1 is x32
// parallelism, valid across the board's 3.3 V supply range.
void Unlock()
{
    if(FLASH->CR1 & FLASH_CR_LOCK)
    {
        FLASH->KEYR1 = 0x45670123u;
        FLASH->KEYR1 = 0xCDEF89ABu;
    }
}

bool FlashBusy()
{
    return (FLASH->SR1 & FLASH_FLAG_QW_BANK1) != 0;
}

void ClearFlashFlags()
{
    __HAL_FLASH_CLEAR_FLAG_BANK1(FLASH_FLAG_ALL_ERRORS_BANK1 | FLASH_FLAG_EOP_BANK1);
}

bool FlashErrors()
{
    return (FLASH->SR1 & FLASH_FLAG_ALL_ERRORS_BANK1) != 0;
}

// Erasing runs in the background so the main loop keeps drawing the LEDs
void ProcessErase()
{
    if(erase_running)
    {
        if(FlashBusy())
            return;
        const bool ok = !FlashErrors();
        ClearFlashFlags();
        FLASH->CR1 &= ~(FLASH_CR_SER | FLASH_CR_SNB);
        erase_running = false;
        SectorDone(1u << erase_sector, ok);
        if(!ok)
            Fail();
        return;
    }

    const uint32_t pending = pending_sectors;
    if(pending == 0)
        return;
    if(failed)
    {
        SectorDone(pending, false);
        return;
    }
    erase_sector = 31u - __CLZ(pending & (~pending + 1u)); // lowest set bit
    Unlock();
    ClearFlashFlags();
    FLASH->CR1 &= ~(FLASH_CR_PSIZE | FLASH_CR_SNB);
    FLASH->CR1 |= FLASH_CR_SER | FLASH_CR_PSIZE_1
                  | ((kFirstFlashSector + erase_sector) << FLASH_CR_SNB_Pos) | FLASH_CR_START;
    erase_running = true;
}

bool ProgramAndVerify(uint32_t addr, const uint8_t* data, uint32_t len)
{
    Unlock();
    ClearFlashFlags();
    FLASH->CR1 &= ~FLASH_CR_PSIZE;
    FLASH->CR1 |= FLASH_CR_PG | FLASH_CR_PSIZE_1;
    bool ok = true;
    for(uint32_t off = 0; ok && off < len; off += kFlashWord)
    {
        volatile uint32_t* dst = reinterpret_cast<volatile uint32_t*>(addr + off);
        for(uint32_t w = 0; w < kFlashWord / 4; w++)
        {
            uint32_t value;
            std::memcpy(&value, data + off + w * 4, 4);
            dst[w] = value;
        }
        __DSB();
        while(FlashBusy()) {}
        ok = !FlashErrors();
    }
    FLASH->CR1 &= ~FLASH_CR_PG;
    ClearFlashFlags();
    if(!ok)
        return false;
    __DSB();
    return std::memcmp(reinterpret_cast<const void*>(addr), data, len) == 0;
}

void ProcessWrite()
{
    if(!write_pending)
        return;

    uint32_t addr = write_addr;
    uint32_t len  = write_len;

    // A short final chunk is padded: flash programs whole words only
    const uint32_t padded = (len + kFlashWord - 1) / kFlashWord * kFlashWord;
    std::memset(chunk + len, 0xFF, padded - len);

    const uint8_t* data = chunk;
    uint32_t       todo = padded;
    if(addr == AUDREY_APP_BASE)
    {
        std::memcpy(held_word, chunk, kFlashWord);
        held_valid = true;
        data += kFlashWord;
        addr += kFlashWord;
        todo -= kFlashWord;
    }

    if(ProgramAndVerify(addr, data, todo))
        written = written + len;
    else
        Fail();
    write_pending = false;
}

void ProcessLeave()
{
    if(!leave_requested || pending_sectors != 0 || erase_running || write_pending)
        return;

    // USB has already disconnected. On a failure the LEDs say so until power-off.
    if(failed)
        return;
    if(held_valid && !ProgramAndVerify(AUDREY_APP_BASE, held_word, kFlashWord))
    {
        Fail();
        return;
    }
    audrey::LedStatusOff();
    __disable_irq();
    NVIC_SystemReset();
}

void ProcessStall()
{
    if(upload_active && !leave_requested
       && static_cast<int32_t>(HAL_GetTick() - deadline_ms) > 0)
        upload_active = false; // the image stays uncommitted; the next erase starts over
}

void FillBusyStatus(uint8_t* buffer, uint32_t poll_ms)
{
    buffer[0] = failed ? kStatusVerify : kStatusOk;
    buffer[1] = static_cast<uint8_t>(poll_ms);
    buffer[2] = static_cast<uint8_t>(poll_ms >> 8);
    buffer[3] = static_cast<uint8_t>(poll_ms >> 16);
    buffer[4] = kStateDnBusy;
    buffer[5] = 0;
}

// Runs in the USB interrupt
void StartUpload()
{
    upload_active = true;
    data_seen     = false;
    failed        = false;
    held_valid    = false;
    expected      = 0;
    written       = 0;
    std::memset(page_bits, 0, sizeof(page_bits));
    // pending_sectors stays: nothing else changes it here, and an erase in progress
    // can't be stopped
    erased_sectors = 0;
}
} // namespace

extern "C"
{
    // All of these run in the USB interrupt
    static uint16_t MEM_If_Init(void)
    {
        return kMediaOk;
    }

    static uint16_t MEM_If_DeInit(void)
    {
        return kMediaOk;
    }

    static uint16_t MEM_If_Erase(uint32_t add)
    {
        if(!InAppRegion(add, 1) || write_pending)
            return kMediaFail;

        // A new upload unless the last one is still erasing. After writes the flash
        // is no longer blank, so a retry must erase again.
        if(!upload_active || data_seen)
            StartUpload();
        deadline_ms = HAL_GetTick() + kStallMs;

        const uint32_t page = (add - AUDREY_APP_BASE) / kVirtualPage;
        if(!(page_bits[page / 32] & (1u << (page % 32))))
        {
            page_bits[page / 32] |= 1u << (page % 32);
            expected = expected + kVirtualPage;
        }

        const uint32_t bit = 1u << SectorOf(add);
        if(!((pending_sectors | erased_sectors) & bit))
            pending_sectors = pending_sectors | bit;
        return kMediaOk;
    }

    static uint16_t MEM_If_Write(uint8_t* src, uint8_t* dest, uint32_t len)
    {
        const uint32_t addr = reinterpret_cast<uint32_t>(dest);
        // Writes are always to erased flash: a host that skips the erase can't reach
        // already-programmed flash words. A host that ignores busy is refused here.
        if(failed || write_pending || pending_sectors != 0 || len == 0
           || len > USBD_DFU_XFER_SIZE || addr % kFlashWord != 0
           || !InAppRegion(addr, len)
           || !(erased_sectors & (1u << SectorOf(addr)))
           || !(erased_sectors & (1u << SectorOf(addr + len - 1))))
            return kMediaFail;

        std::memcpy(chunk, src, len);
        write_addr    = addr;
        write_len     = len;
        upload_active = true;
        data_seen     = true;
        deadline_ms   = HAL_GetTick() + kStallMs;
        write_pending = true;
        return kMediaOk;
    }

    static uint8_t* MEM_If_Read(uint8_t* src, uint8_t* dest, uint32_t len)
    {
        const uint32_t addr = reinterpret_cast<uint32_t>(src);
        if(InAppRegion(addr, len))
            std::memcpy(dest, src, len);
        else
            std::memset(dest, 0xFF, len);
        return dest;
    }

    // Runs before the request it reports on
    static uint16_t MEM_If_GetStatus(uint32_t add, uint8_t cmd, uint8_t* buffer)
    {
        (void)add;
        (void)cmd;
        deadline_ms = HAL_GetTick() + kStallMs;
        FillBusyStatus(buffer, kRequestPollMs);
        return kMediaOk;
    }

    // The middleware has already disconnected USB. Flash work belongs to the main
    // loop, which may still be busy.
    static uint16_t MEM_If_Leave(uint32_t add)
    {
        (void)add;
        leave_requested = true;
        return kMediaOk;
    }

    USBD_DFU_MediaTypeDef USBD_DFU_fops_FS = {
        (uint8_t*)AUDREY_FLASH_LAYOUT,
        MEM_If_Init,
        MEM_If_DeInit,
        MEM_If_Erase,
        MEM_If_Write,
        MEM_If_Read,
        MEM_If_GetStatus,
        MEM_If_Leave,
    };
}

namespace
{
USBD_ClassTypeDef dfu_class;
uint8_t           busy_status[6];

// The DFU class reports dfuDNBUSY once per request, for one bwPollTimeout, so that
// would have to cover the slowest possible erase. DFU lets a device stay in dfuDNBUSY
// across polls and hosts keep polling, so while queued flash work is running, answer
// GETSTATUS here. The class stays in dfuDNLOAD-SYNC and takes over again when it's
// done, and the host waits exactly as long as the flash takes.
uint8_t DfuSetup(USBD_HandleTypeDef* pdev, USBD_SetupReqTypedef* req)
{
    const auto* hdfu
        = static_cast<USBD_DFU_HandleTypeDef*>(pdev->pClassDataCmsit[pdev->classId]);
    const bool status_request = (req->bmRequest & USB_REQ_TYPE_MASK) == USB_REQ_TYPE_CLASS
                                && req->bRequest == DFU_GETSTATUS;
    const bool erasing = pending_sectors != 0;

    if(status_request && hdfu != nullptr && hdfu->dev_state == DFU_STATE_DNLOAD_SYNC
       && hdfu->wlength == 0 && (erasing || write_pending))
    {
        deadline_ms = HAL_GetTick() + kStallMs;
        FillBusyStatus(busy_status, erasing ? kEraseBusyPollMs : kWriteBusyPollMs);
        USBD_CtlSendData(pdev, busy_status, sizeof(busy_status));
        return USBD_OK;
    }
    return USBD_DFU.Setup(pdev, req);
}
} // namespace

namespace audrey
{
bool DfuInit()
{
    dfu_class       = USBD_DFU;
    dfu_class.Setup = DfuSetup;

    std::memset(&hUsbDeviceFS, 0, sizeof(hUsbDeviceFS));
    if(USBD_Init(&hUsbDeviceFS, &FS_Desc, DEVICE_FS) != USBD_OK
       || USBD_RegisterClass(&hUsbDeviceFS, &dfu_class) != USBD_OK
       || USBD_DFU_RegisterMedia(&hUsbDeviceFS, &USBD_DFU_fops_FS) != USBD_OK
       || USBD_Start(&hUsbDeviceFS) != USBD_OK)
        return false;
    HAL_PWREx_EnableUSBVoltageDetector();
    UsbInternalNoVbus();
    return true;
}

void DfuProcess()
{
    ProcessErase();
    ProcessWrite();
    ProcessLeave();
    ProcessStall();
}

DfuPhase DfuGetPhase()
{
    if(failed)
        return DfuPhase::Failed;
    if(leave_requested)
        return DfuPhase::Write;
    if(!upload_active)
        return DfuPhase::Idle;
    return data_seen ? DfuPhase::Write : DfuPhase::Erase;
}

float DfuGetProgress()
{
    if(leave_requested)
        return 1.f;
    const uint32_t total = expected;
    if(total == 0)
        return 0.f;
    const float p = static_cast<float>(written) / total;
    return p > 1.f ? 1.f : p;
}
} // namespace audrey
