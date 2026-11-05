// Audrey integrated board support for the bootloader. The clock fallback is copied
// from Audrey-III src/hw/integrated/integrated_seed.cpp and the USB support from
// src/hw/core/core_board.cpp, unchanged apart from dropping the boost option.

#include "board.h"

#include "daisy.h"
#include "usbd_def.h"

using namespace daisy;

extern "C"
{
    extern PCD_HandleTypeDef  hpcd_USB_OTG_FS;
    extern USBD_HandleTypeDef hUsbDeviceFS;
}

namespace
{
// HSE start-up: typically 2 ms (DS12110 tSU(HSE)); give it 200 ms
constexpr uint32_t kHseTimeoutMs = 200;
// CPU clock at reset: HSI 64 MHz, HSIDIV 1
constexpr uint32_t kResetClockHz = 64000000;

uint32_t crs_last_poll  = 0;
uint32_t crs_last_event = 0;

GPIO xsmt_pin, amp_pin, led_en_pin;

// Starts the crystal and waits for it, with a timeout counted in CPU cycles (no tick
// runs yet). Leaves it running when it starts, off when not.
bool StartHse()
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->LAR    = 0xC5ACCE55u; // Cortex-M7: unlock the DWT registers
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    const uint32_t limit = kResetClockHz / 1000u * kHseTimeoutMs;

    RCC->CR |= RCC_CR_HSEON;
    while(!(RCC->CR & RCC_CR_HSERDY))
    {
        if(DWT->CYCCNT > limit)
        {
            RCC->CR &= ~RCC_CR_HSEON;
            return false;
        }
    }
    return true;
}

void ClockError()
{
    while(1) {}
}

// libDaisy's System::ConfigureClocks with the HSI (64 MHz) as the PLL source instead
// of the 16 MHz HSE: each PLL's M divider x 4, so every clock is the same.
void ConfigureClocksHsi()
{
    RCC_OscInitTypeDef       osc  = {};
    RCC_ClkInitTypeDef       clk  = {};
    RCC_PeriphCLKInitTypeDef peri = {};

    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

    __HAL_RCC_PLL_PLLSOURCE_CONFIG(RCC_PLLSOURCE_HSI);
    osc.OscillatorType      = RCC_OSCILLATORTYPE_HSI48 | RCC_OSCILLATORTYPE_HSI;
    osc.HSIState            = RCC_HSI_DIV1;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.HSI48State          = RCC_HSI48_ON;
    osc.PLL.PLLState        = RCC_PLL_ON;
    osc.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
    osc.PLL.PLLM            = 4 * 4;
    osc.PLL.PLLN            = 200;
    osc.PLL.PLLP            = 2;
    osc.PLL.PLLQ            = 5;
    osc.PLL.PLLR            = 2;
    osc.PLL.PLLRGE          = RCC_PLL1VCIRANGE_2;
    osc.PLL.PLLVCOSEL       = RCC_PLL1VCOWIDE;
    osc.PLL.PLLFRACN        = 0;
    if(HAL_RCC_OscConfig(&osc) != HAL_OK)
        ClockError();

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1
                    | RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1
                    | RCC_CLOCKTYPE_D1PCLK1;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.SYSCLKDivider  = RCC_SYSCLK_DIV1;
    clk.AHBCLKDivider  = RCC_HCLK_DIV2;
    clk.APB3CLKDivider = RCC_APB3_DIV2;
    clk.APB1CLKDivider = RCC_APB1_DIV2;
    clk.APB2CLKDivider = RCC_APB2_DIV2;
    clk.APB4CLKDivider = RCC_APB4_DIV2;
    if(HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK)
        ClockError();

    peri.PeriphClockSelection = RCC_PERIPHCLK_USB;
    peri.UsbClockSelection    = RCC_USBCLKSOURCE_HSI48;
    if(HAL_RCCEx_PeriphCLKConfig(&peri) != HAL_OK)
        ClockError();

    HAL_PWREx_EnableUSBVoltageDetector();
}

// libDaisy's System::ConfigureMpu (private there), for the D2 SRAM1 DMA buffers.
// skip_clocks skips it too.
void ConfigureMpu()
{
    MPU_Region_InitTypeDef mpu;
    HAL_MPU_Disable();
    mpu.Enable           = MPU_REGION_ENABLE;
    mpu.BaseAddress      = 0x30000000;
    mpu.Size             = MPU_REGION_SIZE_32KB;
    mpu.AccessPermission = MPU_REGION_FULL_ACCESS;
    mpu.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
    mpu.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
    mpu.IsShareable      = MPU_ACCESS_SHAREABLE;
    mpu.Number           = MPU_REGION_NUMBER0;
    mpu.TypeExtField     = MPU_TEX_LEVEL1;
    mpu.SubRegionDisable = 0x00;
    mpu.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
    HAL_MPU_ConfigRegion(&mpu);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

// HSI48 (USB clock) is outside USB full speed's +/-0.25 % untrimmed. The ROM loader
// trims it from USB SOF with the CRS, and so do we.
void EnableCrs()
{
    __HAL_RCC_CRS_CLK_ENABLE();
    RCC_CRSInitTypeDef crs    = {};
    crs.Prescaler             = RCC_CRS_SYNC_DIV1;
    crs.Source                = RCC_CRS_SYNC_SOURCE_USB2; // OTG_FS SOF, 1 kHz
    crs.Polarity              = RCC_CRS_SYNC_POLARITY_RISING;
    crs.ReloadValue           = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000U, 1000U);
    crs.ErrorLimitValue       = RCC_CRS_ERRORLIMIT_DEFAULT;
    crs.HSI48CalibrationValue = RCC_CRS_HSI48CALIBRATION_DEFAULT;
    HAL_RCCEx_CRSConfig(&crs);
    crs_last_event = HAL_GetTick();
}
} // namespace

namespace audrey
{
void BoardInit()
{
    xsmt_pin.Init(pins::kDacXsmt, GPIO::Mode::OUTPUT);
    xsmt_pin.Write(false);
    amp_pin.Init(pins::kAmpEn, GPIO::Mode::OUTPUT);
    amp_pin.Write(false);
    // R125 holds it low until now; drive it as early as the app does
    led_en_pin.Init(pins::kLedEn, GPIO::Mode::OUTPUT);
    led_en_pin.Write(false);

    System::Config cfg;
    cfg.Defaults();
    // Flash contents are read back for verification
    cfg.use_dcache = false;

    if(!StartHse())
    {
        HAL_Init(); // tick for the HAL clock functions' timeouts
        ConfigureClocksHsi();
        ConfigureMpu();
        cfg.skip_clocks = true;
    }
    System system;
    system.Init(cfg);
}

void UsbInternalNoVbus()
{
    // What HAL USB_DevInit() does when vbus_sensing_enable = DISABLE: VBUS detection
    // off, B-session valid forced
    __disable_irq();
    hpcd_USB_OTG_FS.Init.vbus_sensing_enable = DISABLE;
    USB_OTG_FS->GCCFG &= ~USB_OTG_GCCFG_VBDEN;
    USB_OTG_FS->GOTGCTL |= USB_OTG_GOTGCTL_BVALOEN | USB_OTG_GOTGCTL_BVALOVAL;
    USB_OTG_FS->GINTMSK &= ~(USB_OTG_GINTMSK_SRQIM | USB_OTG_GINTMSK_OTGINT);
    __enable_irq();

    // PA9 (OTG_FS_VBUS) isn't connected: pull it down rather than leave libDaisy's
    // floating input
    GPIO_InitTypeDef g = {};
    g.Pin              = GPIO_PIN_9;
    g.Mode             = GPIO_MODE_INPUT;
    g.Pull             = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOA, &g);

    EnableCrs();
}

// SYNCSRC encoding: the HAL has 11 = USB2 (OTG_FS) SOF, RM0433 Rev 8 lists 00. A wrong
// choice only means no trimming, so if no SYNC event arrives for 3 s while USB is
// configured, try the other one.
void CrsPoll(uint32_t now_ms)
{
    if(now_ms - crs_last_poll < 500)
        return;
    crs_last_poll = now_ms;

    const uint32_t isr = CRS->ISR;
    if(isr & (CRS_ISR_SYNCOKF | CRS_ISR_SYNCWARNF))
    {
        crs_last_event = now_ms;
    }
    else if(now_ms - crs_last_event > 3000)
    {
        if(hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED)
        {
            const uint32_t src = (CRS->CFGR & CRS_CFGR_SYNCSRC) >> CRS_CFGR_SYNCSRC_Pos;
            const uint32_t next = src == 3u ? 0u : 3u;
            CRS->CR &= ~CRS_CR_CEN; // CFGR is writable only with the counter off
            CRS->CFGR = (CRS->CFGR & ~CRS_CFGR_SYNCSRC) | (next << CRS_CFGR_SYNCSRC_Pos);
            CRS->CR |= CRS_CR_CEN;
        }
        crs_last_event = now_ms;
    }
    CRS->ICR = CRS_ICR_SYNCOKC | CRS_ICR_SYNCWARNC | CRS_ICR_ERRC | CRS_ICR_ESYNCC;
}
} // namespace audrey
