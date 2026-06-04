#include "HapticsWrapper.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace
{

struct HapticStep
{
    uint8_t  level;
    uint16_t duration_ms;
};

struct HapticPattern
{
    const HapticStep* steps;
    size_t            step_count;
};

constexpr HapticStep kClickSteps[] = {
    {255, 40},
};

constexpr HapticStep kBuzzSteps[] = {
    {170, 80},
    {0, 35},
    {170, 80},
};

constexpr HapticStep kDoneSteps[] = {
    {120, 35},
    {0, 45},
    {210, 120},
};

constexpr HapticPattern kClickPattern = {kClickSteps, sizeof(kClickSteps) / sizeof(kClickSteps[0])};
constexpr HapticPattern kBuzzPattern  = {kBuzzSteps, sizeof(kBuzzSteps) / sizeof(kBuzzSteps[0])};
constexpr HapticPattern kDonePattern  = {kDoneSteps, sizeof(kDoneSteps) / sizeof(kDoneSteps[0])};

std::atomic<bool> g_haptic_busy = {false};

void play_pattern_blocking(const HapticPattern& pattern)
{
    for (size_t i = 0; i < pattern.step_count; ++i)
    {
        M5.Power.setVibration(pattern.steps[i].level);
        delay(pattern.steps[i].duration_ms);
    }
    M5.Power.setVibration(0);
}

void haptic_task(void* arg)
{
    const HapticPattern* pattern = static_cast<const HapticPattern*>(arg);
    if (pattern)
    {
        play_pattern_blocking(*pattern);
    }

    g_haptic_busy.store(false, std::memory_order_release);
    vTaskDelete(nullptr);
}

bool haptic_play_pattern(const HapticPattern& pattern)
{
    bool expected = false;
    if (!g_haptic_busy.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
    {
        return false;
    }

    if (xTaskCreate(haptic_task, "haptic", 2048, const_cast<HapticPattern*>(&pattern), 1, nullptr) != pdPASS)
    {
        play_pattern_blocking(pattern);
        g_haptic_busy.store(false, std::memory_order_release);
    }

    return true;
}

} // namespace

bool haptic_play_click()
{
    return haptic_play_pattern(kClickPattern);
}

bool haptic_play_buzz()
{
    return haptic_play_pattern(kBuzzPattern);
}

bool haptic_play_done()
{
    return haptic_play_pattern(kDonePattern);
}
