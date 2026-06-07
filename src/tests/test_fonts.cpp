#include <Arduino.h>
#include <M5Unified.h>
#include <stdio.h>

namespace
{

constexpr const char* TAG             = "test_fonts";
constexpr int16_t     kStartX         = 5;
constexpr int16_t     kStartY         = 5;
constexpr int16_t     kLineGap        = 2;
constexpr uint8_t     kFirstFont      = 0;
constexpr uint8_t     kLastFont       = 8;
constexpr uint8_t     kScreenCountMax = 3;

const char* sample_text()
{
    return "abcde CODE 012345";
}

bool draw_font_line(uint8_t font, float size, int16_t& y)
{
    M5.Display.setTextFont(font);
    M5.Display.setTextSize(size);
    M5.Display.setTextDatum(top_left);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);

    const int16_t line_height = static_cast<int16_t>(M5.Display.fontHeight() + kLineGap);
    if (y + line_height > M5.Display.height())
    {
        return false;
    }

    char line[64];
    snprintf(line,
             sizeof(line),
             "font %u %.1fx %s",
             static_cast<unsigned>(font),
             static_cast<double>(size),
             sample_text());
    M5.Display.drawString(line, kStartX, y);
    y = static_cast<int16_t>(y + line_height);
    return true;
}

int16_t font_line_height(uint8_t font, float size)
{
    M5.Display.setTextFont(font);
    M5.Display.setTextSize(size);
    return static_cast<int16_t>(M5.Display.fontHeight() + kLineGap);
}

void start_screen(uint8_t screen, float size_a, float size_b)
{
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextFont(1);
    M5.Display.setTextSize(1.0f);
    M5.Display.setTextDatum(top_left);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);

    char title[64];
    snprintf(title,
             sizeof(title),
             "font test page %u: %.1fx and %.1fx",
             static_cast<unsigned>(screen + 1),
             static_cast<double>(size_a),
             static_cast<double>(size_b));
    M5.Display.drawString(title, kStartX, kStartY);
}

void wait_for_touch_advance()
{
    while (true)
    {
        M5.update();
        if (!M5.Touch.getDetail().isPressed())
        {
            break;
        }
        delay(10);
    }

    while (true)
    {
        M5.update();
        if (M5.Touch.getDetail().wasPressed())
        {
            break;
        }
        delay(10);
    }
}

} // namespace

void test_fonts()
{
    Serial.begin(115200, SERIAL_8N1, -1, 1);
    delay(1000);

    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.setBrightness(255);
    M5.Display.setColorDepth(16);
    M5.Display.fillScreen(TFT_BLACK);

    Serial.println();
    Serial.printf("%s: starting font sampler\n", TAG);

    uint8_t font   = kFirstFont;
    uint8_t screen = 0;

    while (true)
    {
        const float size_a = 1.0f + static_cast<float>(screen);
        const float size_b = 2.0f + static_cast<float>(screen);
        start_screen(screen, size_a, size_b);

        int16_t y = static_cast<int16_t>(kStartY + 14);
        while (font <= kLastFont)
        {
            const int16_t pair_height =
                static_cast<int16_t>(font_line_height(font, size_a) + font_line_height(font, size_b));
            if (y + pair_height > M5.Display.height())
            {
                break;
            }

            if (!draw_font_line(font, size_a, y) || !draw_font_line(font, size_b, y))
            {
                break;
            }

            Serial.printf("%s: page=%u font=%u sizes=%.1f,%.1f\n",
                          TAG,
                          static_cast<unsigned>(screen + 1),
                          static_cast<unsigned>(font),
                          static_cast<double>(size_a),
                          static_cast<double>(size_b));
            ++font;
        }

        wait_for_touch_advance();

        ++screen;
        if (screen >= kScreenCountMax || font > kLastFont)
        {
            screen = 0;
            font   = kFirstFont;
        }
    }
}
