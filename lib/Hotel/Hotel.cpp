#include "Hotel.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_timer.h>

#include "Display.h"
#include "../AudioFileRecorder/AudioFileRecorder.h"
#include "../AudioManager/AudioManager.h"
#include "../BluetoothManager/BluetoothManager.h"

namespace Hotel
{
namespace
{

constexpr const char* TAG = "Hotel";

constexpr uint32_t kCpuMaxMhz          = 240;
constexpr uint32_t kCpuMinMhz          = 80;
constexpr uint8_t  kFullBrightness     = 255;
constexpr uint8_t  kDimBrightness      = 32;
constexpr uint64_t kFullCpuMs          = 10 * 1000ULL;
constexpr uint64_t kLightSleepMs       = 30 * 1000ULL;
constexpr uint64_t kFullBrightnessMs   = 60 * 1000ULL;
constexpr uint64_t kShutdownMs         = 5 * 60 * 1000ULL;
constexpr uint64_t kLightSleepWakeUs   = 50 * 1000ULL;
constexpr uint32_t kSyncWindowMs       = 20;
constexpr int      kHotelLocalLogLevel = static_cast<int>(LOG_LOCAL_LEVEL);
#ifdef CORE_DEBUG_LEVEL
constexpr int kHotelCoreLogLevel = CORE_DEBUG_LEVEL;
#else
constexpr int kHotelCoreLogLevel = static_cast<int>(ESP_LOG_NONE);
#endif
constexpr bool kFullShutdownAllowedByLogging =
    kHotelLocalLogLevel <= static_cast<int>(ESP_LOG_ERROR) &&
    kHotelCoreLogLevel <= static_cast<int>(ESP_LOG_ERROR);

portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;

State    g_state                 = State::RecentlyActive;
uint64_t g_last_activity_ms      = 0;
uint32_t g_last_core_poll_ms[2]  = {};
bool     g_core_ready[2]         = {};
bool     g_initialized           = false;
bool     g_shutdown_requested    = false;

uint64_t monotonic_ms()
{
    return static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);
}

bool bluetooth_active()
{
    return BtManager::state() != BtManager::State::Idle;
}

bool wifi_active()
{
    return WiFi.getMode() != WIFI_MODE_NULL;
}

bool audio_active()
{
    return AudioFileRecorder::isRecording() || AudioManager::mode() != AudioManager::P2TMode::Stopped;
}

bool power_saving_blocked()
{
    return audio_active() || bluetooth_active() || wifi_active();
}

State desired_state(uint64_t now_ms, bool blocked)
{
    if (blocked)
    {
        return State::Blocked;
    }

    const uint64_t inactive_ms = now_ms - g_last_activity_ms;
    if (inactive_ms >= kShutdownMs)
    {
        return kFullShutdownAllowedByLogging ? State::Shutdown : State::DimLightSleepReady;
    }
    if (inactive_ms >= kFullBrightnessMs)
    {
        return State::DimLightSleepReady;
    }
    if (inactive_ms >= kLightSleepMs)
    {
        return State::LightSleepReady;
    }
    if (inactive_ms >= kFullCpuMs)
    {
        return State::ActiveDimAllowed;
    }
    return State::RecentlyActive;
}

bool state_allows_light_sleep(State value)
{
    return value == State::LightSleepReady || value == State::DimLightSleepReady;
}

bool state_uses_full_brightness(State value)
{
    return value == State::Blocked || value == State::RecentlyActive || value == State::ActiveDimAllowed || value == State::LightSleepReady;
}

bool state_uses_max_cpu(State value)
{
    return value == State::Blocked || value == State::RecentlyActive;
}

bool m5_ready()
{
    return M5.getBoard() != m5gfx::board_t::board_unknown;
}

bool ensure_initialized_locked(uint64_t now_ms)
{
    if (g_initialized)
    {
        return false;
    }

    g_initialized          = true;
    g_last_activity_ms     = now_ms;
    g_last_core_poll_ms[0] = static_cast<uint32_t>(now_ms);
    g_last_core_poll_ms[1] = static_cast<uint32_t>(now_ms);
    return true;
}

void apply_power_outputs(State previous, State next)
{
    if (state_uses_max_cpu(previous) != state_uses_max_cpu(next))
    {
        setCpuFrequencyMhz(state_uses_max_cpu(next) ? kCpuMaxMhz : kCpuMinMhz);
    }

    if (state_uses_full_brightness(previous) != state_uses_full_brightness(next))
    {
        if (m5_ready())
        {
            thefly_display.setBrightness(state_uses_full_brightness(next) ? kFullBrightness : kDimBrightness);
        }
    }
}

void apply_initialized_outputs()
{
    setCpuFrequencyMhz(kCpuMaxMhz);
    if (m5_ready())
    {
        thefly_display.setBrightness(kFullBrightness);
    }
    ESP_LOGD(TAG, "initialized");
}

void enter_light_sleep()
{
    if (m5_ready())
    {
        M5.Power.lightSleep(kLightSleepWakeUs, true);
        return;
    }

    esp_sleep_enable_timer_wakeup(kLightSleepWakeUs);
    esp_light_sleep_start();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
}

void poll_core(uint8_t core)
{
    const uint64_t now_ms = monotonic_ms();
    const bool     blocked = power_saving_blocked();
    bool           sleep        = false;
    bool           init_outputs = false;
    bool           state_changed = false;
    bool           shutdown      = false;
    State          previous      = State::RecentlyActive;
    State          next          = State::RecentlyActive;

    portENTER_CRITICAL(&g_lock);
    init_outputs = ensure_initialized_locked(now_ms);

    next = desired_state(now_ms, blocked);
    if (next != g_state)
    {
        previous        = g_state;
        state_changed   = true;
        g_state         = next;
        g_core_ready[0] = false;
        g_core_ready[1] = false;

        if (next == State::Shutdown && !g_shutdown_requested)
        {
            g_shutdown_requested = true;
            shutdown             = true;
        }
    }

    if (state_allows_light_sleep(g_state))
    {
        const uint8_t other = core == 0 ? 1 : 0;
        g_core_ready[core]  = true;

        const uint32_t now32          = static_cast<uint32_t>(now_ms);
        const bool     other_is_fresh = static_cast<uint32_t>(now32 - g_last_core_poll_ms[other]) <= kSyncWindowMs;
        if (g_core_ready[other] && other_is_fresh)
        {
            g_core_ready[0] = false;
            g_core_ready[1] = false;
            sleep           = true;
        }
    }
    else
    {
        g_core_ready[0] = false;
        g_core_ready[1] = false;
    }

    g_last_core_poll_ms[core] = static_cast<uint32_t>(now_ms);
    portEXIT_CRITICAL(&g_lock);

    if (init_outputs)
    {
        apply_initialized_outputs();
    }

    if (state_changed)
    {
        ESP_LOGD(TAG, "state transition: %s -> %s", stateName(previous), stateName(next));
        apply_power_outputs(previous, next);
    }

    if (shutdown)
    {
        ESP_LOGD(TAG, "powering off");
        if (m5_ready())
        {
            M5.Power.powerOff();
        }
        else
        {
            esp_deep_sleep_start();
        }
    }

    if (sleep)
    {
        enter_light_sleep();
    }
}

} // namespace

void init()
{
    const uint64_t now_ms = monotonic_ms();
    bool           init_outputs;

    portENTER_CRITICAL(&g_lock);
    init_outputs = ensure_initialized_locked(now_ms);
    portEXIT_CRITICAL(&g_lock);

    if (init_outputs)
    {
        apply_initialized_outputs();
    }
}

void pollCore0()
{
    poll_core(0);
}

void pollCore1()
{
    poll_core(1);
}

void noteUserActivity()
{
    const uint64_t now_ms = monotonic_ms();
    bool           init_outputs;

    portENTER_CRITICAL(&g_lock);
    init_outputs          = ensure_initialized_locked(now_ms);
    g_last_activity_ms   = now_ms;
    g_shutdown_requested = false;
    portEXIT_CRITICAL(&g_lock);

    if (init_outputs)
    {
        apply_initialized_outputs();
    }
}

void noteUserActivityFromIsr()
{
    const uint64_t now_ms = monotonic_ms();
    portENTER_CRITICAL_ISR(&g_lock);
    ensure_initialized_locked(now_ms);
    g_last_activity_ms   = now_ms;
    g_shutdown_requested = false;
    portEXIT_CRITICAL_ISR(&g_lock);
}

State state()
{
    portENTER_CRITICAL(&g_lock);
    const State value = g_state;
    portEXIT_CRITICAL(&g_lock);
    return value;
}

const char* stateName(State value)
{
    switch (value)
    {
    case State::Blocked:
        return "Blocked";
    case State::RecentlyActive:
        return "RecentlyActive";
    case State::ActiveDimAllowed:
        return "ActiveDimAllowed";
    case State::LightSleepReady:
        return "LightSleepReady";
    case State::DimLightSleepReady:
        return "DimLightSleepReady";
    case State::Shutdown:
        return "Shutdown";
    default:
        return "Unknown";
    }
}

} // namespace Hotel
