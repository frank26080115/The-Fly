#include "Hotel.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include "dbg_log.h"
#include <esp_sleep.h>
#include <esp_timer.h>

#include "Display.h"
#include "../AudioFileRecorder/AudioFileRecorder.h"
#include "../AudioManager/AudioManager.h"
#include "../BluetoothManager/BluetoothManager.h"
#include "../FlyGuiViews/ShutdownView.h"
#include "../WavPlayback/FilePlayback.h"

namespace Hotel
{
namespace
{

constexpr const char* TAG = "Hotel";

constexpr uint32_t kCpuMaxMhz                    = 240;
constexpr uint32_t kCpuMinMhz                    = 80;
constexpr uint8_t  kFullBrightness               = 255;
constexpr uint8_t  kDimBrightness                = 32;
constexpr uint64_t kFullCpuMs                    = 10 * 1000ULL;
constexpr uint64_t kLightSleepMs                 = 30 * 1000ULL;
constexpr uint64_t kFullBrightnessMs             = 60 * 1000ULL;
constexpr uint64_t kShutdownMs                   = 5 * 60 * 1000ULL;
constexpr uint64_t kLightSleepWakeUs             = 50 * 1000ULL;
constexpr uint32_t kSyncWindowMs                 = 20;
constexpr int      kHotelLocalLogLevel           = static_cast<int>(DBG_LOG_LOCAL_LEVEL);
constexpr bool     kFullShutdownAllowedByLogging = kHotelLocalLogLevel <= static_cast<int>(DBG_LOG_ERROR);
#if defined(ENABLE_HOTEL_DEEP_POWER_SAVE)
constexpr bool kDeepPowerSaveEnabled = true;
#else
constexpr bool kDeepPowerSaveEnabled = false;
#endif
// Deep power save is intentionally compile-time gated. CPU frequency changes
// have broken Bluetooth HFP setup, and Core2/M5Unified light sleep has caused
// RTCWDT resets after idle. Leave ENABLE_HOTEL_DEEP_POWER_SAVE undefined for
// normal firmware builds.
constexpr bool kLightSleepSupported        = false;
constexpr bool kCpuFrequencyScalingAllowed = kDeepPowerSaveEnabled;
constexpr bool kLightSleepAllowedByLogging =
    kDeepPowerSaveEnabled && kLightSleepSupported && kFullShutdownAllowedByLogging;

portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;

State    g_state                 = State::RecentlyActive;
uint64_t g_last_activity_ms      = 0;
uint32_t g_last_core_poll_ms[2]  = {};
bool     g_core_ready[2]         = {};
bool     g_initialized           = false;
bool     g_shutdown_requested    = false;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static void     poll_core(uint8_t core);
static void     apply_power_outputs(State previous, State next);
static void     apply_initialized_outputs();
static void     enter_light_sleep();
static State    desired_state(uint64_t now_ms, bool full_blocked, bool shutdown_blocked);
static bool     ensure_initialized_locked(uint64_t now_ms);
static bool     full_power_saving_blocked();
static bool     audio_blocks_full_power_saving();
static bool     bluetooth_active();
static bool     wifi_active();
static bool     state_allows_light_sleep(State value);
static bool     state_uses_full_brightness(State value);
static bool     state_uses_max_cpu(State value);
static bool     m5_ready();
static uint64_t monotonic_ms();

} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

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
    init_outputs         = ensure_initialized_locked(now_ms);
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

namespace
{

// -----------------------------------------------------------------------------
// Feature Logic
// -----------------------------------------------------------------------------

void poll_core(uint8_t core)
{
    const uint64_t now_ms           = monotonic_ms();
    const bool     full_blocked     = full_power_saving_blocked();
    const bool     playback_playing = FilePlayback::filePlaybackPlaying();
    const bool     sleep_blocked    = full_blocked || playback_playing;
    const bool     shutdown_blocked = sleep_blocked;
    bool           sleep            = false;
    bool           init_outputs     = false;
    bool           state_changed    = false;
    bool           shutdown         = false;
    State          previous         = State::RecentlyActive;
    State          next             = State::RecentlyActive;

    portENTER_CRITICAL(&g_lock);
    init_outputs = ensure_initialized_locked(now_ms);

    next = desired_state(now_ms, full_blocked, shutdown_blocked);
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

    if (!sleep_blocked && state_allows_light_sleep(g_state))
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
        DBG_LOGD(TAG, "state transition: %s -> %s", stateName(previous), stateName(next));
        apply_power_outputs(previous, next);
    }

    if (shutdown)
    {
        DBG_LOGD(TAG, "powering off");
        ShutdownView::showSleepAndShutdown();
    }

    if (sleep)
    {
        enter_light_sleep();
    }
}

void apply_power_outputs(State previous, State next)
{
    if (kCpuFrequencyScalingAllowed && state_uses_max_cpu(previous) != state_uses_max_cpu(next))
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
    if (kCpuFrequencyScalingAllowed)
    {
        setCpuFrequencyMhz(kCpuMaxMhz);
    }
    if (m5_ready())
    {
        thefly_display.setBrightness(kFullBrightness);
    }
    DBG_LOGD(TAG, "initialized");
}

void enter_light_sleep()
{
    if (!kDeepPowerSaveEnabled)
    {
        return;
    }

    if (m5_ready())
    {
        M5.Power.lightSleep(kLightSleepWakeUs, true);
        return;
    }

    esp_sleep_enable_timer_wakeup(kLightSleepWakeUs);
    esp_light_sleep_start();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

State desired_state(uint64_t now_ms, bool full_blocked, bool shutdown_blocked)
{
    if (full_blocked)
    {
        return State::Blocked;
    }

    const uint64_t inactive_ms = now_ms - g_last_activity_ms;
    if (inactive_ms >= kShutdownMs)
    {
        return kFullShutdownAllowedByLogging && !shutdown_blocked ? State::Shutdown : State::DimLightSleepReady;
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

bool full_power_saving_blocked()
{
    return audio_blocks_full_power_saving() || bluetooth_active() || wifi_active();
}

bool audio_blocks_full_power_saving()
{
    return AudioFileRecorder::isRecording() || AudioManager::mode() == AudioManager::P2TMode::Mic;
}

bool bluetooth_active()
{
    return BtManager::state() != BtManager::State::Idle;
}

bool wifi_active()
{
    return WiFi.getMode() != WIFI_MODE_NULL;
}

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

bool state_allows_light_sleep(State value)
{
    if (!kLightSleepAllowedByLogging)
    {
        return false;
    }
    return value == State::LightSleepReady || value == State::DimLightSleepReady;
}

bool state_uses_full_brightness(State value)
{
    return value == State::Blocked || value == State::RecentlyActive || value == State::ActiveDimAllowed ||
           value == State::LightSleepReady;
}

bool state_uses_max_cpu(State value)
{
    return value == State::Blocked || value == State::RecentlyActive;
}

bool m5_ready()
{
    return M5.getBoard() != m5gfx::board_t::board_unknown;
}

uint64_t monotonic_ms()
{
    return static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);
}

} // namespace

} // namespace Hotel
