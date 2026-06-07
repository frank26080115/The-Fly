#include <Arduino.h>
#include <M5Unified.h>

#include "HapticsWrapper.h"

namespace
{

constexpr const char* TAG = "test_hapticfeedback";

void draw_test_screen()
{
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextDatum(top_left);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.drawString("Haptic feedback", 12, 12);
    M5.Display.setTextSize(1);
    M5.Display.drawString("BtnA: click", 12, 52);
    M5.Display.drawString("BtnB: buzz", 12, 72);
    M5.Display.drawString("BtnC: done", 12, 92);
}

void report_result(const char* name, bool started)
{
    Serial.printf("%s: %s %s\n", TAG, name, started ? "started" : "busy");

    M5.Display.fillRect(12, 124, M5.Display.width() - 24, 24, TFT_BLACK);
    M5.Display.setTextColor(started ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
    M5.Display.drawString(started ? name : "busy", 12, 124);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}

} // namespace

void test_hapticfeedback()
{
    Serial.begin(115200, SERIAL_8N1, -1, 1);
    delay(1000);

    auto cfg = M5.config();
    M5.begin(cfg);

    M5.BtnA.setDebounceThresh(20);
    M5.BtnB.setDebounceThresh(20);
    M5.BtnC.setDebounceThresh(20);

    draw_test_screen();

    Serial.println();
    Serial.printf("%s: BtnA=click BtnB=buzz BtnC=done\n", TAG);

    while (true)
    {
        M5.update();

        if (M5.BtnA.wasPressed())
        {
            report_result("click", haptic_play_click());
        }
        if (M5.BtnB.wasPressed())
        {
            report_result("buzz", haptic_play_buzz());
        }
        if (M5.BtnC.wasPressed())
        {
            report_result("done", haptic_play_done());
        }

        delay(1);
    }
}
