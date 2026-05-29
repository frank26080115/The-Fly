#include <Arduino.h>
#include <M5Unified.h>

#include "FlyGui.h"
#include "SpriteDraw.h"

#include "all_tests.h"
#include "sprites.h"

extern FlyGui* gui;

namespace
{

constexpr const char* TAG = "test_pngdecode";

void on_sprite_draw_callback()
{
    taskYIELD();
}

void report_draw_result(const SpriteDraw::DrawResult& result)
{
    Serial.printf("%s: draw %s: decoded=%lux%lu callbacks=%lu pixels=%lu elapsed=%lu.%03lu ms\n",
                  TAG,
                  result.ok ? "ok" : "failed",
                  static_cast<unsigned long>(result.decoded_width),
                  static_cast<unsigned long>(result.decoded_height),
                  static_cast<unsigned long>(result.callbacks),
                  static_cast<unsigned long>(result.pixels),
                  static_cast<unsigned long>(result.elapsed_us / 1000U),
                  static_cast<unsigned long>(result.elapsed_us % 1000U));
}

} // namespace

void test_pngdecode()
{
    Serial.begin(115200);
    delay(1000);

    auto cfg = M5.config();
    M5.begin(cfg);

    gui = new FlyGui();
    thefly_display.fillScreen(TFT_BLACK);

    Serial.println();
    Serial.printf("%s: starting PNG decode test\n", TAG);

    bool use_fast = true;

    while (true)
    {
        thefly_display.fillScreen(TFT_BLACK);

        const int32_t splash_x = (thefly_display.width() - static_cast<int32_t>(SPRITE_SPLASH_WIDTH)) / 2;
        const int32_t splash_y = (thefly_display.height() - static_cast<int32_t>(SPRITE_SPLASH_HEIGHT)) / 2;
        const SpriteDraw::DrawResult result =
            SpriteDraw::drawPng(sprite_splash,
                                SPRITE_SPLASH_BYTES,
                                splash_x,
                                splash_y,
                                SPRITE_SPLASH_WIDTH,
                                SPRITE_SPLASH_HEIGHT,
                                use_fast,
                                SpriteDraw::PNG_BRTNESS_100,
                                on_sprite_draw_callback
                            );
        report_draw_result(result);

        delay(3000);
        thefly_display.fillScreen(TFT_BLACK);
        delay(3000);

        use_fast = !use_fast;
    }
}
