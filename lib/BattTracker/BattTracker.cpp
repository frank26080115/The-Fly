#include "BattTracker.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <math.h>

#include "../FlyGuiViews/ShutdownView.h"

namespace BattTracker
{
namespace
{
constexpr uint32_t kPollIntervalMs             = 1000;
constexpr float    kLowThreshold               = 3.40f;
constexpr float    kEmergencyShutdownThreshold = 3.00f;
constexpr float    kHighThreshold              = 3.95f;
constexpr float    kChargeHysteresis           = 0.10f;
constexpr float    kUnknownVoltage             = -1.0f;

uint32_t    last_poll_ms    = 0;
float       tracked_voltage = kUnknownVoltage;
bool        charging        = false;
bool        usb_available   = false;
ChargeLevel tracked_level   = ChargeLevel::unknown;
bool        initialized     = false;
uint32_t    start_time      = 0;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static void        updateNow();
static bool        halReadBatteryCharging();
static ChargeLevel levelFromVoltage(float volts);
static ChargeLevel levelFromChargingVoltage(float volts, ChargeLevel current);

} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void init()
{
    initialized     = true;
    last_poll_ms    = millis();
    tracked_voltage = kUnknownVoltage;
    charging        = false;
    usb_available   = false;
    tracked_level   = ChargeLevel::unknown;
    start_time      = last_poll_ms;
    updateNow();
}

void poll()
{
    if (!initialized)
    {
        init();
        shutdownIfNeeded();
        return;
    }

    const uint32_t now = millis();
    if (now - last_poll_ms < kPollIntervalMs)
    {
        return;
    }

    last_poll_ms = now;
    updateNow();
    shutdownIfNeeded();
}

bool shutdownRequired()
{
    return initialized && !charging && isfinite(tracked_voltage) && tracked_voltage > 0.0f &&
           tracked_voltage <= kEmergencyShutdownThreshold;
}

void shutdownIfNeeded()
{
    if (shutdownRequired())
    {
        ShutdownView::showLowBatteryAndShutdown();
    }
}

Status status()
{
    uint8_t value = STATUS_UNKNOWN;
    switch (tracked_level)
    {
    case ChargeLevel::low:
        value = STATUS_LOW;
        break;
    case ChargeLevel::medium:
        value = STATUS_MEDIUM;
        break;
    case ChargeLevel::high:
        value = STATUS_HIGH;
        break;
    case ChargeLevel::unknown:
    default:
        value = STATUS_UNKNOWN;
        break;
    }

    if (charging && value != STATUS_UNKNOWN)
    {
        value |= STATUS_CHARGING_BIT;
    }
    return static_cast<Status>(value);
}

ChargeLevel level()
{
    return tracked_level;
}

bool isCharging()
{
    return charging;
}

bool isUsbAvailable()
{
    return usb_available;
}

float voltage()
{
    return tracked_voltage;
}

namespace
{

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

void updateNow()
{
    const float measured_voltage  = halReadBatteryVoltage();
    const bool  measured_charging = halReadBatteryCharging();

    charging      = measured_charging;
    usb_available = halReadUsbAvailable();

    if (!isfinite(measured_voltage) || measured_voltage <= 0.0f)
    {
        tracked_voltage = kUnknownVoltage;
        tracked_level   = ChargeLevel::unknown;
        return;
    }

    if (tracked_voltage <= 0.0f)
    {
        tracked_voltage = measured_voltage;
    }
    else if (measured_charging)
    {
        tracked_voltage = measured_voltage;
    }
    else if (measured_voltage < tracked_voltage)
    {
        tracked_voltage = measured_voltage;
    }

    tracked_level = measured_charging ? levelFromChargingVoltage(tracked_voltage, tracked_level)
                                      : levelFromVoltage(tracked_voltage);
}

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

ChargeLevel levelFromVoltage(float volts)
{
    if (!isfinite(volts) || volts <= 0.0f)
    {
        return ChargeLevel::unknown;
    }
    if (volts <= kLowThreshold)
    {
        return ChargeLevel::low;
    }
    if (volts >= kHighThreshold)
    {
        return ChargeLevel::high;
    }
    return ChargeLevel::medium;
}

ChargeLevel levelFromChargingVoltage(float volts, ChargeLevel current)
{
    if (!isfinite(volts) || volts <= 0.0f)
    {
        return ChargeLevel::unknown;
    }

    switch (current)
    {
    case ChargeLevel::low:
        return volts >= kLowThreshold + kChargeHysteresis ? ChargeLevel::medium : ChargeLevel::low;

    case ChargeLevel::medium:
        if (volts <= kLowThreshold - kChargeHysteresis)
        {
            return ChargeLevel::low;
        }
        if (volts >= kHighThreshold + kChargeHysteresis)
        {
            return ChargeLevel::high;
        }
        return ChargeLevel::medium;

    case ChargeLevel::high:
        return volts <= kHighThreshold - kChargeHysteresis ? ChargeLevel::medium : ChargeLevel::high;

    case ChargeLevel::unknown:
    default:
        return levelFromVoltage(volts);
    }
}

bool halReadBatteryCharging()
{
#ifndef TEST_SIM_BATTERY
    return M5.Power.isCharging() == m5::Power_Class::is_charging;
#else
    return false;
#endif
}

} // namespace

// -----------------------------------------------------------------------------
// HAL Helpers
// -----------------------------------------------------------------------------

float halReadBatteryVoltage()
{
    int16_t millivolts;
#ifndef TEST_SIM_BATTERY
    millivolts = M5.Power.getBatteryVoltage();
#else
    constexpr uint32_t kSimDrainDurationMs = 2 * 60 * 1000UL;
    constexpr int32_t  kSimFullMv          = 4300;
    constexpr int32_t  kSimEmptyMv         = 2800;

    const uint32_t t_since   = millis() - start_time;
    const uint32_t clamped_t = t_since < kSimDrainDurationMs ? t_since : kSimDrainDurationMs;
    const int32_t  drained_mv =
        ((kSimFullMv - kSimEmptyMv) * static_cast<int32_t>(clamped_t)) / static_cast<int32_t>(kSimDrainDurationMs);
    millivolts = static_cast<int16_t>(kSimFullMv - drained_mv);
#endif
    if (millivolts <= 0)
    {
        return kUnknownVoltage;
    }
    return millivolts / 1000.0f;
}

bool halReadUsbAvailable()
{
#ifndef TEST_SIM_BATTERY
    switch (M5.Power.getType())
    {
#if !defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(CONFIG_IDF_TARGET_ESP32C3) &&                                      \
    !defined(CONFIG_IDF_TARGET_ESP32C6) && !defined(CONFIG_IDF_TARGET_ESP32P4)
    case m5::Power_Class::pmic_axp192:
        return M5.Power.Axp192.isACIN();
#endif

#if defined(CONFIG_IDF_TARGET_ESP32S3) ||                                                                                 \
    (!defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32C6) &&                                       \
     !defined(CONFIG_IDF_TARGET_ESP32P4))
    case m5::Power_Class::pmic_axp2101:
        return M5.Power.Axp2101.isVBUS();
#endif

    default:
        return M5.Power.getVBUSVoltage() > 4000;
    }
#else
    return false;
#endif
}
} // namespace BattTracker
