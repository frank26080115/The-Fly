#include <Arduino.h>
#include <M5Unified.h>

#include "FlyGui.h"

namespace
{

constexpr const char* TAG             = "test_screen";
constexpr uint32_t    kReportPeriodMs = 2000;
constexpr uint16_t    kHueCount       = 360;
constexpr uint8_t     kSaturation     = 255;
constexpr uint8_t     kValue          = 128;

uint16_t hsv_to_rgb565(uint16_t hue, uint8_t saturation, uint8_t value)
{
    hue %= kHueCount;

    const uint8_t region    = hue / 60U;
    const uint8_t remainder = static_cast<uint8_t>(((hue % 60U) * 255U) / 60U);

    const uint8_t p = static_cast<uint8_t>((static_cast<uint16_t>(value) * (255U - saturation)) / 255U);
    const uint8_t q = static_cast<uint8_t>(
        (static_cast<uint16_t>(value) * (255U - ((static_cast<uint16_t>(saturation) * remainder) / 255U))) / 255U);
    const uint8_t t = static_cast<uint8_t>(
        (static_cast<uint16_t>(value) * (255U - ((static_cast<uint16_t>(saturation) * (255U - remainder)) / 255U))) /
        255U);

    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    switch (region)
    {
    case 0:
        r = value;
        g = t;
        b = p;
        break;
    case 1:
        r = q;
        g = value;
        b = p;
        break;
    case 2:
        r = p;
        g = value;
        b = t;
        break;
    case 3:
        r = p;
        g = q;
        b = value;
        break;
    case 4:
        r = t;
        g = p;
        b = value;
        break;
    default:
        r = value;
        g = p;
        b = q;
        break;
    }

    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void report_fps(uint32_t frames, uint32_t elapsed_ms)
{
    const float fps = elapsed_ms == 0 ? 0.0f : (static_cast<float>(frames) * 1000.0f) / static_cast<float>(elapsed_ms);
    Serial.printf("%s: frames=%lu elapsed=%lu ms fps=%.2f\n",
                  TAG,
                  static_cast<unsigned long>(frames),
                  static_cast<unsigned long>(elapsed_ms),
                  fps);
}

} // namespace

void test_screen()
{
    Serial.begin(115200);
    delay(1000);

    auto cfg = M5.config();
    M5.begin(cfg);

    FlyGui gui;

    thefly_display.setBrightness(255);
    thefly_display.setColorDepth(16);
    thefly_display.fillScreen(TFT_BLACK);

    Serial.println();
    Serial.printf("%s: starting full-screen HSV fill test: %dx%d\n",
                  TAG,
                  thefly_display.width(),
                  thefly_display.height());

    uint16_t hue               = 0;
    uint32_t report_started_ms = millis();
    uint32_t frames            = 0;

    while (true)
    {
        thefly_display.fillScreen(hsv_to_rgb565(hue, kSaturation, kValue));
        hue = static_cast<uint16_t>((hue + 1U) % kHueCount);
        ++frames;

        const uint32_t now_ms = millis();
        if (static_cast<uint32_t>(now_ms - report_started_ms) >= kReportPeriodMs)
        {
            report_fps(frames, now_ms - report_started_ms);
            frames            = 0;
            report_started_ms = now_ms;
        }
    }
}
