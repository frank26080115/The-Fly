#include "ShutdownView.h"

#include "Display.h"
#include "SpriteDraw.h"
#include "sprites.h"

#include "../AudioFileRecorder/AudioFileRecorder.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <atomic>
#include <esp_sleep.h>

namespace ShutdownView
{
namespace
{

constexpr uint32_t kShutdownDelayMs = 3000;
constexpr uint8_t  kShutdownBrightness = 255;

std::atomic<bool> g_shutdown_in_progress(false);

bool m5_ready()
{
    return M5.getBoard() != m5gfx::board_t::board_unknown;
}

void idle_forever()
{
    while (true)
    {
        delay(1000);
    }
}

void draw_centered_icon(const uint8_t* sprite, size_t bytes, uint32_t width, uint32_t height)
{
    thefly_display.setBrightness(kShutdownBrightness);
    thefly_display.fillScreen(TFT_BLACK);

    const int32_t x = (thefly_display.width() - static_cast<int32_t>(width)) / 2;
    const int32_t y = (thefly_display.height() - static_cast<int32_t>(height)) / 2;
    SpriteDraw::drawPng(sprite, bytes, x, y, width, height, true);
}

void power_off()
{
    if (m5_ready())
    {
        M5.Power.powerOff();
    }

    esp_deep_sleep_start();
    idle_forever();
}

void emergency_stop_recording()
{
    if (AudioFileRecorder::isRecording())
    {
        AudioFileRecorder::stopRecording(true);
    }
}

void show_and_shutdown(const uint8_t* sprite, size_t bytes, uint32_t width, uint32_t height)
{
    if (g_shutdown_in_progress.exchange(true))
    {
        idle_forever();
    }

    draw_centered_icon(sprite, bytes, width, height);
    emergency_stop_recording();
    delay(kShutdownDelayMs);
    power_off();
}

} // namespace

bool shutdownInProgress()
{
    return g_shutdown_in_progress.load();
}

void showSleepAndShutdown()
{
    show_and_shutdown(sprite_sleep_100, SPRITE_SLEEP_100_BYTES, SPRITE_SLEEP_100_WIDTH, SPRITE_SLEEP_100_HEIGHT);
}

void showLowBatteryAndShutdown()
{
    show_and_shutdown(sprite_lowbatt_100, SPRITE_LOWBATT_100_BYTES, SPRITE_LOWBATT_100_WIDTH, SPRITE_LOWBATT_100_HEIGHT);
}

} // namespace ShutdownView
