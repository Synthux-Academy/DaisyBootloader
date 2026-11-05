#include "led_status.h"

#include "board.h"
#include "daisy.h"
#include "ws2812.h"

using namespace daisy;

namespace
{
// The whole chain: 8 on the board plus the 32-LED ring. WS2812s hold their last
// colour, so the ring must be sent too or the app's ring would stay lit.
constexpr uint8_t kNumLeds = 40;

// Chain indexes of the plates in progress order, from Audrey-III src/ui/core.ui.h:
// kPadLed[kTapPadDucking] .. kPadLed[kTapPadBodyTrack], plate 6 to plate 1.
constexpr uint8_t kProgressLeds[]  = {6, 5, 4, 3, 1, 0};
constexpr int     kNumProgressLeds = sizeof(kProgressLeds);

constexpr uint32_t kWhite = 0xFFFFFF;
constexpr uint32_t kRed   = 0xFF0000;

constexpr float kBrightness = 0.5f;
// Audrey-III's value on this board. The driver's gamma applies after it.
constexpr float kBrightnessCap = 0.8f;

constexpr uint32_t kGlowPeriodMs  = 2000;
constexpr uint32_t kBlinkPeriodMs = 1000;
// Faster than the eye: the driver dithers over 4 frames at low levels
constexpr uint32_t kFrameMs = 5;

// The LED rail takes about 2 s to discharge after power-off, and until then the LEDs
// remember their last colours, showing them as soon as power returns. So black goes
// out before LED power comes on, to overwrite them while they can't light, and keeps
// going until the rail is up in case some LEDs missed it.
constexpr uint32_t kBlackBeforePowerMs = 5;
constexpr uint32_t kBlackAfterPowerMs  = 100;
// The same black frames work in the Audrey app, which reaches this point at least
// 25 ms after start-up (it waits for the codec rails). This code would get here
// within a few ms, and the LEDs then still show their old colours. Time since
// HAL_Init, so it doesn't apply on top of a slower start.
constexpr uint32_t kEarliestLedStartMs = 50;
// A reset cuts any frame short and floats the data line. Let the rail fall first.
constexpr uint32_t kPowerDownMs = 50;

infrasonic::Ws2812 leds;
GPIO               led_en;
uint32_t           last_frame_ms = 0;

// The driver ignores Show while a frame is on its way
void SendBlack(uint32_t ms)
{
    const uint32_t start = System::GetNow();
    while(System::GetNow() - start < ms)
        leds.Show();
}

void SetProgressLeds(uint32_t color, float brightness)
{
    for(int i = 0; i < kNumProgressLeds; i++)
        leds.Set(kProgressLeds[i], color, brightness);
}
} // namespace

namespace audrey
{
void LedStatusInit()
{
    while(System::GetNow() < kEarliestLedStartMs) {}

    infrasonic::Ws2812::Config cfg;
    cfg.num_leds    = kNumLeds;
    cfg.tim_pin     = pins::kLedData;
    cfg.tim_periph  = TimerHandle::Config::Peripheral::TIM_3;
    cfg.tim_channel = infrasonic::Ws2812::Config::CH1;
    leds.Init(cfg);
    leds.SetBrightnessLimit(kBrightnessCap);

    led_en.Init(pins::kLedEn, GPIO::Mode::OUTPUT);
    led_en.Write(false);
    SendBlack(kBlackBeforePowerMs);
    led_en.Write(true);
    SendBlack(kBlackAfterPowerMs);
}

void LedStatusOff()
{
    leds.Clear();
    // Two whole frames: one may already be on its way, showing the old colours
    SendBlack(2 * kFrameMs);
    led_en.Write(false);
    System::Delay(kPowerDownMs);
}

void LedStatusUpdate(uint32_t now_ms, DfuPhase phase, float progress)
{
    if(now_ms - last_frame_ms < kFrameMs)
        return;
    last_frame_ms = now_ms;

    leds.Clear();
    switch(phase)
    {
        case DfuPhase::Idle:
        {
            const uint32_t t     = now_ms % kGlowPeriodMs;
            const uint32_t half  = kGlowPeriodMs / 2;
            const float    level = (t < half ? t : kGlowPeriodMs - t) / float(half);
            SetProgressLeds(kWhite, kBrightness * level);
            break;
        }
        case DfuPhase::Erase:
            leds.Set(kProgressLeds[0], kWhite, kBrightness);
            break;
        case DfuPhase::Write:
        {
            // Plate 6 stays lit; the other five each fade in over their 1/5 of progress
            leds.Set(kProgressLeds[0], kWhite, kBrightness);
            const float steps = progress * (kNumProgressLeds - 1);
            for(int i = 1; i < kNumProgressLeds; i++)
            {
                float level = steps - (i - 1);
                level       = level < 0.f ? 0.f : (level > 1.f ? 1.f : level);
                leds.Set(kProgressLeds[i], kWhite, kBrightness * level);
            }
            break;
        }
        case DfuPhase::Failed:
            if(now_ms % kBlinkPeriodMs < kBlinkPeriodMs / 2)
                SetProgressLeds(kRed, kBrightness);
            break;
    }
    leds.Show();
}
} // namespace audrey
