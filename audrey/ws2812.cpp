// Copyright 2024 Infrasonic Audio LLC
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
// Copied from Audrey-III src/hw/ws2812.cpp. The only change from that copy: this
// driver owns its timer's HAL handle, so it builds against stock libDaisy.
#include "ws2812.h"
#include <sys/dma.h>
#include <sys/system.h>
#include <util/hal_map.h>
#include <algorithm>
#include <cmath>
#include <cmsis_gcc.h>

using namespace daisy;
using namespace infrasonic;

namespace infrasonic
{
// 3 colors * 8 values per LED plus 8 leading/trailing zeros
static constexpr size_t kPwmOutBufSize = Ws2812::kMaxNumLEDs * 3 * 8 + 16;
static uint16_t DMA_BUFFER_MEM_SECTION pwm_out_buf[kPwmOutBufSize];

static constexpr uint8_t kDitherBits = 2;

// A frame of 128 LEDs takes under 4 ms. With no transfer-complete by then (a
// DMA error ends the transfer without one), Show() aborts it and starts a new
// one, so the LEDs can't stay stuck on the last frame.
static constexpr uint32_t kTransferTimeoutMs = 20;
static_assert(kDitherBits <= 4);

uint32_t get_dma_req(Ws2812::Config::TimerPeripheral periph,
                     Ws2812::Config::TimerChannel    chn)
{
    switch(periph)
    {
        case TimerHandle::Config::Peripheral::TIM_3:
            switch(chn)
            {
                case Ws2812::Config::TimerChannel::CH1:
                    return DMA_REQUEST_TIM3_CH1;
                case Ws2812::Config::TimerChannel::CH2:
                    return DMA_REQUEST_TIM3_CH2;
                case Ws2812::Config::TimerChannel::CH3:
                    return DMA_REQUEST_TIM3_CH3;
                case Ws2812::Config::TimerChannel::CH4:
                    return DMA_REQUEST_TIM3_CH4;
            }
            break;
        case TimerHandle::Config::Peripheral::TIM_4:
            switch(chn)
            {
                case Ws2812::Config::TimerChannel::CH1:
                    return DMA_REQUEST_TIM4_CH1;
                case Ws2812::Config::TimerChannel::CH2:
                    return DMA_REQUEST_TIM4_CH2;
                case Ws2812::Config::TimerChannel::CH3:
                    return DMA_REQUEST_TIM4_CH3;
                case Ws2812::Config::TimerChannel::CH4:
                    // Invalid
                    break;
            }
            break;
        default: break;
    }
    return 0;
}

/** Private impl class for single static shared instance */
class Ws2812::Impl
{
  public:
    Impl()  = default;
    ~Impl() = default;

    void Init(const Ws2812::Config& config);

    void Set(uint8_t idx, uint8_t r, uint8_t g, uint8_t b, uint8_t brightness)
    {
        if(idx >= num_leds_)
            return;
        led_data_[idx][0] = r;
        led_data_[idx][1] = g;
        led_data_[idx][2] = b;
        brightness_[idx]  = brightness * blimit_;
    }

    void Show();

    void SetGamma(float gamma)
    {
        for(size_t i = 0; i < 256; i++)
        {
            gammaCorrectionLUT_8Bit_[i] = static_cast<uint8_t>(
                roundf(255.0f * powf(i / 255.0f, gamma)));
        }
    }

    void SetBrightnessLimit(float limit)
    {
        blimit_ = std::clamp(limit, 0.1f, 1.0f);
    }

    static void OnTransferEnd(Ws2812::Impl* impl) { impl->dma_ready_ = true; }

    TIM_HandleTypeDef htim;
    DMA_HandleTypeDef timhdma;

  private:
    uint32_t           timch_;
    volatile uint32_t* timccr_;

    uint8_t  num_leds_;
    uint16_t zero_period_;
    uint16_t one_period_;

    uint16_t* dma_buffer_;
    size_t    dma_buffer_size_;

    uint8_t led_data_[Ws2812::kMaxNumLEDs][3]; /**< RGB data */
    uint8_t brightness_[Ws2812::kMaxNumLEDs];

    volatile bool dma_ready_; // set by the transfer-complete interrupt
    uint32_t      transfer_start_ms_;

    uint8_t gammaCorrectionLUT_8Bit_[256];
    int16_t dither_;

    float blimit_;

    void initPwmDma(const Ws2812::Config& config)
    {
        // This really only works with TIM3 and TIM4
        // TODO: need error handling of some kind
        uint32_t gpio_af;
        switch(config.tim_periph)
        {
            case Ws2812::Config::TimerPeripheral::TIM_3:
                gpio_af = GPIO_AF2_TIM3;
                break;
            case Ws2812::Config::TimerPeripheral::TIM_4:
                gpio_af = GPIO_AF2_TIM4;
                break;
            default: return;
        }

        uint16_t dma_id;
        switch(config.tim_channel)
        {
            case Ws2812::Config::TimerChannel::CH1:
                timch_  = TIM_CHANNEL_1;
                timccr_ = &htim.Instance->CCR1;
                dma_id  = TIM_DMA_ID_CC1;
                break;
            case Ws2812::Config::TimerChannel::CH2:
                timch_  = TIM_CHANNEL_2;
                timccr_ = &htim.Instance->CCR2;
                dma_id  = TIM_DMA_ID_CC2;
                break;
            case Ws2812::Config::TimerChannel::CH3:
                timch_  = TIM_CHANNEL_3;
                timccr_ = &htim.Instance->CCR3;
                dma_id  = TIM_DMA_ID_CC3;
                break;
            case Ws2812::Config::TimerChannel::CH4:
                timch_  = TIM_CHANNEL_4;
                timccr_ = &htim.Instance->CCR4;
                dma_id  = TIM_DMA_ID_CC4;
                break;
            default: return;
        }

        uint32_t dmareq = get_dma_req(config.tim_periph, config.tim_channel);
        if(dmareq == 0)
            return;

        /** Configure Channel */
        TIM_OC_InitTypeDef sConfigOC = {0};
        sConfigOC.OCMode             = TIM_OCMODE_PWM1;
        sConfigOC.Pulse              = 0;
        sConfigOC.OCPolarity         = TIM_OCPOLARITY_HIGH;
        sConfigOC.OCNPolarity        = TIM_OCNPOLARITY_HIGH;
        sConfigOC.OCFastMode         = TIM_OCFAST_DISABLE;
        sConfigOC.OCIdleState        = TIM_OCIDLESTATE_RESET;
        sConfigOC.OCNIdleState       = TIM_OCNIDLESTATE_RESET;

        HAL_TIM_PWM_ConfigChannel(&htim, &sConfigOC, timch_);

        TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig;
        sBreakDeadTimeConfig.OffStateRunMode  = TIM_OSSR_DISABLE;
        sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
        sBreakDeadTimeConfig.LockLevel        = TIM_LOCKLEVEL_OFF;
        sBreakDeadTimeConfig.DeadTime         = 0;
        sBreakDeadTimeConfig.BreakState       = TIM_BREAK_DISABLE;
        sBreakDeadTimeConfig.BreakPolarity    = TIM_BREAKPOLARITY_HIGH;
        sBreakDeadTimeConfig.AutomaticOutput  = TIM_AUTOMATICOUTPUT_DISABLE;
        HAL_TIMEx_ConfigBreakDeadTime(&htim, &sBreakDeadTimeConfig);

        Pin           tpin = config.tim_pin;
        GPIO_TypeDef* port = GetHALPort(tpin);
        uint16_t      pin  = GetHALPin(tpin);

        /** Start Clock for port (if necessary) */
        GPIOClockEnable(tpin);

        /** Intilize the actual pin */
        GPIO_InitTypeDef gpio_init;
        gpio_init           = {0};
        gpio_init.Pin       = pin;
        gpio_init.Mode      = GPIO_MODE_AF_PP;
        gpio_init.Pull      = GPIO_NOPULL;
        gpio_init.Speed     = GPIO_SPEED_LOW;
        gpio_init.Alternate = gpio_af;
        HAL_GPIO_Init(port, &gpio_init);

        HAL_NVIC_SetPriority(DMA2_Stream5_IRQn, 1, 0);
        HAL_NVIC_EnableIRQ(DMA2_Stream5_IRQn);

        // NOTE: Lots of hardcoded stuff here
        timhdma.Instance = DMA2_Stream5;

        timhdma.Init.Request             = dmareq;
        timhdma.Init.Direction           = DMA_MEMORY_TO_PERIPH;
        timhdma.Init.PeriphInc           = DMA_PINC_DISABLE;
        timhdma.Init.MemInc              = DMA_MINC_ENABLE;
        timhdma.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
        timhdma.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
        timhdma.Init.Mode                = DMA_NORMAL;
        timhdma.Init.Priority            = DMA_PRIORITY_LOW;
        timhdma.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
        timhdma.Init.MemBurst            = DMA_MBURST_SINGLE;
        timhdma.Init.PeriphBurst         = DMA_PBURST_SINGLE;
        if(HAL_DMA_Init(&timhdma) != HAL_OK)
        {
            // something bad
            asm("bkpt 255");
        }
        __HAL_LINKDMA(&htim, hdma[dma_id], timhdma);
    }

    void populateBits(uint8_t color_val, uint16_t* buff)
    {
        uint8_t mask = 0x80;
        for(int i = 0; i < 8; i++)
        {
            buff[i] = (color_val & mask) > 0 ? one_period_ : zero_period_;
            mask    = mask >> 1;
        }
    }

    bool isDMAReady() { return dma_ready_; }

    void fillDMABuffer()
    {
        for(uint32_t i = 0; i < num_leds_; i++)
        {
            /** Grab G, R, B for filling bytes */
            // TODO: Alt color order?
            uint8_t shift = 8 - kDitherBits;

            // Apply 8-bit brightness and shift according to dither depth
            // (i.e. if 2 bits of dither, we shift so multiplication is a 10-bit number)
            uint16_t g = (led_data_[i][1] * brightness_[i]) >> shift;
            uint16_t r = (led_data_[i][0] * brightness_[i]) >> shift;
            uint16_t b = (led_data_[i][2] * brightness_[i]) >> shift;

            // apply dither and truncate
            g = g > 0 ? __USAT((g + dither_) >> kDitherBits, 8) : 0;
            r = r > 0 ? __USAT((r + dither_) >> kDitherBits, 8) : 0;
            b = b > 0 ? __USAT((b + dither_) >> kDitherBits, 8) : 0;

            // gamma correction
            g = gammaCorrectionLUT_8Bit_[g];
            r = gammaCorrectionLUT_8Bit_[r];
            b = gammaCorrectionLUT_8Bit_[b];

            size_t data_index = i * 3 * 8 + 8;
            populateBits(g, &dma_buffer_[data_index]);
            populateBits(r, &dma_buffer_[data_index + 8]);
            populateBits(b, &dma_buffer_[data_index + 16]);
        }

        dither_ = (dither_ + 1) & ((1 << kDitherBits) - 1);
        // dither_ = rand() & ((1 << kDitherBits) - 1);
    }
};

static Ws2812::Impl impl;

} // namespace infrasonic

void Ws2812::Impl::Init(const Ws2812::Config& config)
{
    SetGamma(2.6f);

    uint32_t prescaler         = 4;
    uint32_t tickspeed         = (System::GetPClk1Freq() * 2) / prescaler;
    uint32_t target_pulse_freq = 1e9 / config.symbol_length_ns;
    uint16_t period            = (tickspeed / target_pulse_freq) - 1;

    // The same set-up as TimerHandle::Init, SetPrescaler and SetPeriod. libDaisy's
    // HAL_TIM_Base_MspInit enables the timer clock.
    htim.Instance = config.tim_periph == Ws2812::Config::TimerPeripheral::TIM_4
                        ? TIM4
                        : TIM3;
    htim.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim.Init.Prescaler         = prescaler - 1;
    htim.Init.Period            = period;
    htim.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_Base_Init(&htim);

    TIM_ClockConfigTypeDef clock_cfg = {0};
    clock_cfg.ClockSource            = TIM_CLOCKSOURCE_INTERNAL;
    HAL_TIM_ConfigClockSource(&htim, &clock_cfg);

    TIM_MasterConfigTypeDef master_cfg = {0};
    master_cfg.MasterOutputTrigger     = TIM_TRGO_RESET;
    master_cfg.MasterSlaveMode         = TIM_MASTERSLAVEMODE_DISABLE;
    HAL_TIMEx_MasterConfigSynchronization(&htim, &master_cfg);

    initPwmDma(config);

    HAL_TIM_Base_Start(&htim);

    num_leds_ = std::min(config.num_leds, Ws2812::kMaxNumLEDs);
    zero_period_
        = period * ((float)config.zero_high_ns / config.symbol_length_ns);
    one_period_
        = period * ((float)config.one_high_ns / config.symbol_length_ns);

    dma_buffer_      = pwm_out_buf;
    dma_buffer_size_ = num_leds_ * 3 * 8 + 16;
    dither_          = 0;

    blimit_ = 1.0f;

    for(size_t i = 0; i < dma_buffer_size_; i++)
    {
        dma_buffer_[i] = 0;
    }

    // Start DMA request ONCE using this HAL method -
    // from here on just generate the request directly
    // using HAL_DMA_Start_IT
    dma_ready_         = false;
    transfer_start_ms_ = System::GetNow();
    HAL_TIM_PWM_Start_DMA(
        &htim, timch_, (uint32_t*)dma_buffer_, dma_buffer_size_);
}

void Ws2812::Impl::Show()
{
    if(!isDMAReady())
    {
        if(System::GetNow() - transfer_start_ms_ < kTransferTimeoutMs)
            return;
        // Lost transfer: stop whatever is left of it (no-op if the stream is idle)
        HAL_DMA_Abort(&timhdma);
    }
    dma_ready_         = false;
    transfer_start_ms_ = System::GetNow();
    fillDMABuffer();

    // Just start the new DMA request - don't use
    // HAL_TIM_PWM_Start_DMA, it's overzealous and leads
    // to occasional LED blips due to repeated CCR enable.
    // If it doesn't start, the timeout above retries.
    HAL_DMA_Start_IT(&timhdma,
                     (uint32_t)dma_buffer_,
                     (uint32_t) & (*timccr_),
                     dma_buffer_size_);
}

// -------

void Ws2812::Init(const Config& config)
{
    pimpl_ = &impl;
    pimpl_->Init(config);
    num_leds_ = config.num_leds;
    Clear();
}

void Ws2812::SetGamma(float gamma)
{
    pimpl_->SetGamma(gamma);
}

void Ws2812::SetBrightnessLimit(float limit)
{
    pimpl_->SetBrightnessLimit(limit);
}

void Ws2812::Set(uint8_t idx, uint8_t r, uint8_t g, uint8_t b, float brightness)
{
    brightness = std::min(std::max(brightness, 0.0f), 1.0f);
    pimpl_->Set(idx, r, g, b, brightness * 255);
}

void Ws2812::Set(uint8_t idx, uint32_t color, float brightness)
{
    color = (color & 0x00FFFFFF);
    Set(idx, color >> 16, color >> 8, color, brightness);
}

void Ws2812::Fill(uint32_t color, float brightness)
{
    for(uint8_t i = 0; i < num_leds_; i++)
    {
        Set(i, color, brightness);
    }
}

void Ws2812::Clear()
{
    Fill(0);
}

void Ws2812::Show()
{
    pimpl_->Show();
}

// ---- C HAL Callbacks ----

// These are a bit dangerous to put directly in a specific device class.
// Should be globally managed along with other timers HAL callbacks and
// dispatch appropriate user callback based on device configuration.

extern "C" void DMA2_Stream5_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&impl.timhdma);
}

extern "C" void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef* htim)
{
    Ws2812::Impl::OnTransferEnd(&impl);
}

extern "C" void DMAMUX1_OVR_IRQHandler(void) {}
