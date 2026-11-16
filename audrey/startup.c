// Audrey bootloader start-up. See ../specs/audrey-bootloader.md, "Boot flow".
//
// Flash holds a small vector table and the boot flow below, which decides between
// the app and the bootloader with nothing set up yet. In bootloader mode it copies
// the rest of the image to AXI SRAM and runs libDaisy's Reset_Handler from there.

#include "boot_layout.h"
#include "stm32h7xx.h"
#include "sw2_hold.h"

// libDaisy's start-up file, unchanged: the full vector table (g_pfnVectors, in
// .isr_vector, in RAM) and its reset handler, which clears .bss, calls SystemInit,
// the constructors and main. Renamed so it isn't the chip's reset handler.
#define Reset_Handler Ram_Reset_Handler
#include "core/startup_stm32h750xx.c"
#undef Reset_Handler

#define BOOT_TEXT __attribute__((section(".boot_text"), used))

extern uint32_t _sram_image, _eram_image, _siram_image;

static inline volatile uint32_t* BootRequest()
{
    return (volatile uint32_t*)AUDREY_BOOT_REQUEST_ADDR;
}

static inline volatile uint32_t* TapFlag()
{
    return (volatile uint32_t*)AUDREY_TAP_FLAG_ADDR;
}

static BOOT_TEXT __attribute__((noreturn)) void SystemReset()
{
    __DSB();
    SCB->AIRCR = (0x5FAu << SCB_AIRCR_VECTKEY_Pos) | SCB_AIRCR_SYSRESETREQ_Msk;
    __DSB();
    for(;;) {}
}

// A fault in the boot flow, such as an ECC error reading the app's vectors: treat
// it as no valid app. Uses the stack only if it still works; a reset follows anyway.
static BOOT_TEXT void BootFault()
{
    *BootRequest() = AUDREY_BOOT_REQUEST_MAGIC;
    SystemReset();
}

static BOOT_TEXT int AppValid()
{
    const volatile uint32_t* app = (const volatile uint32_t*)AUDREY_APP_BASE;
    const uint32_t           sp  = app[0];
    const uint32_t           pc  = app[1];
    return sp > 0x20000000u && sp <= 0x20020000u && (pc & 1u) != 0u
           && pc >= AUDREY_APP_BASE && pc < AUDREY_APP_END;
}

// SW2 (Tap, PC3): low = pressed, 10k pull-up on the board
static BOOT_TEXT int Sw2Down()
{
    return (GPIOC->IDR & GPIO_IDR_ID3) == 0u;
}

// PC3 as an input, then 10 ms at the 64 MHz reset clock for it to settle
static BOOT_TEXT void Sw2Init()
{
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOCEN;
    (void)RCC->AHB4ENR;
    GPIOC->MODER &= ~GPIO_MODER_MODE3;

    SysTick->LOAD = 640000u - 1u;
    SysTick->VAL  = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
    while(!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk)) {}
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;
}

// Leaves GPIOC as at reset
static BOOT_TEXT void Sw2Deinit()
{
    GPIOC->MODER |= GPIO_MODER_MODE3;
    RCC->AHB4ENR &= ~RCC_AHB4ENR_GPIOCEN;
}

#define BOOT_CLOCK_HZ 64000000u // HSI at reset, HSIDIV 1
#define BOOT_CYCLES_PER_MS (BOOT_CLOCK_HZ / 1000u)
_Static_assert(AUDREY_TAP_HOLD_MS + SW2_RELEASE_MS < 0xFFFFFFFFu / BOOT_CYCLES_PER_MS,
               "the hold must fit in the 32-bit cycle counter");

// SW2 was down at the first read: what the hold means (sw2_hold.h), timed with the
// DWT cycle counter at the reset clock (no tick runs yet), as StartHse in board.cpp
// does. Returns after SW2_RELEASE_MS of SW2 up, or as soon as it is still down at
// AUDREY_TAP_HOLD_MS. Leaves the DWT and its trace enable as it found them.
static BOOT_TEXT Sw2Hold Sw2TimeHold()
{
    const uint32_t demcr = CoreDebug->DEMCR;
    CoreDebug->DEMCR     = demcr | CoreDebug_DEMCR_TRCENA_Msk;
    DWT->LAR             = 0xC5ACCE55u; // Cortex-M7: unlock the DWT registers
    const uint32_t ctrl  = DWT->CTRL;
    DWT->CYCCNT          = 0;
    DWT->CTRL            = ctrl | DWT_CTRL_CYCCNTENA_Msk;

    Sw2Timer t;
    t.last_down = 0;
    t.up_since  = 0;
    t.up        = 0;
    Sw2Hold result;
    do
        result = Sw2Step(&t, Sw2Down(), DWT->CYCCNT, BOOT_CYCLES_PER_MS);
    while(result == kSw2Pending);

    DWT->CTRL        = ctrl;
    CoreDebug->DEMCR = demcr;
    return result;
}

static BOOT_TEXT __attribute__((noreturn)) void JumpToApp()
{
    const volatile uint32_t* app = (const volatile uint32_t*)AUDREY_APP_BASE;
    SCB->VTOR                    = AUDREY_APP_BASE;
    __DSB();
    __ISB();
    __enable_irq();
    __asm volatile("msr msp, %0 \n bx %1 \n" : : "r"(app[0]), "r"(app[1]));
    for(;;) {}
}

// volatile: the compiler must not turn the loop into a memcpy call, which is in RAM
static BOOT_TEXT __attribute__((noreturn)) void RunFromRam()
{
    volatile uint32_t*       dst = &_sram_image;
    const volatile uint32_t* src = &_siram_image;
    while(dst < &_eram_image)
        *dst++ = *src++;
    __DSB();
    __ISB();
    // Masked by Reset_Handler. No interrupt is enabled yet, and libDaisy's reset
    // handler expects them unmasked, as after a real reset.
    __enable_irq();
    __asm volatile("ldr r0, =Ram_Reset_Handler \n bx r0 \n" : : : "r0");
    for(;;) {}
}

BOOT_TEXT __attribute__((noreturn, used)) void BootFlow()
{
    const uint32_t request = *BootRequest();
    *BootRequest()         = 0;
    *TapFlag()             = 0;

    // A normal start reads SW2 once, after the same 10 ms settle as before, so a
    // bootloader entry always gets the same delay
    Sw2Init();
    const int     sw2  = Sw2Down();
    const int     boot = request == AUDREY_BOOT_REQUEST_MAGIC || !AppValid();
    Sw2Hold       hold = kSw2Glitch;
    // Tap held at power-on, with an app to start: let go before 5 s, bootloader mode;
    // still held at 5 s, the app starts at once, Tap still down, and flips its
    // auto-sleep setting (its start-up heartbeat shows the new one)
    if(sw2 && !boot)
        hold = Sw2TimeHold();
    Sw2Deinit();

    if(boot || hold == kSw2Bootloader)
        RunFromRam();
    if(hold == kSw2Flip)
        *TapFlag() = AUDREY_TAP_FLAG_MAGIC;
    __DSB();
    JumpToApp();
}

// ST's ROM loader, after "dfu-util ... :leave", jumps here without a reset and with
// its USB device and CRS still running (AN2606). Their clock enables are clear after
// a real reset, so if either is set, reset first and start from a clean chip. The
// same check as Audrey-III's startup_core.c. Registers: RCC_AHB1ENR bit 27
// USB2OTGFSEN, RCC_APB1HENR bit 1 CRSEN, SCB_AIRCR.
__attribute__((section(".boot_text"), noreturn, naked, used)) void Reset_Handler()
{
    __asm volatile(
        "cpsid i                 \n"
        "ldr   r0, =0x580244D8   \n"
        "ldr   r1, [r0]          \n"
        "tst   r1, #(1 << 27)    \n"
        "bne   1f                \n"
        "ldr   r0, =0x580244EC   \n"
        "ldr   r1, [r0]          \n"
        "tst   r1, #(1 << 1)     \n"
        "bne   1f                \n"
        "b     BootFlow          \n"
        "1:                      \n"
        "dsb                     \n"
        "ldr   r0, =0xE000ED0C   \n"
        "ldr   r1, =0x05FA0004   \n"
        "str   r1, [r0]          \n"
        "dsb                     \n"
        "2: b 2b                 \n"
        ".ltorg                  \n");
}

// The table the chip starts from. SystemInit also points VTOR here until main moves
// it to g_pfnVectors. No interrupt is enabled before then, only faults can arrive.
__attribute__((section(".boot_vectors"), used)) void* const g_pfnBootVectors[16] = {
    &_estack,
    (void*)Reset_Handler,
    (void*)BootFault, // NMI
    (void*)BootFault, // HardFault
    (void*)BootFault, // MemManage
    (void*)BootFault, // BusFault
    (void*)BootFault, // UsageFault
};
