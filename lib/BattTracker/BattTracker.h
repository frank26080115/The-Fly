#pragma once

#include "thefly_common.h"

#include <stdint.h>

namespace BattTracker
{
enum class ChargeLevel : uint8_t
{
    unknown = 0,
    low,
    medium,
    high,
};

constexpr uint8_t STATUS_CHARGING_BIT = 0x04;

enum Status : uint8_t
{
    STATUS_UNKNOWN         = 0x00,
    STATUS_LOW             = 0x01,
    STATUS_MEDIUM          = 0x02,
    STATUS_HIGH            = 0x03,
    STATUS_LOW_CHARGING    = 0x05,
    STATUS_MEDIUM_CHARGING = 0x06,
    STATUS_HIGH_CHARGING   = 0x07,
};

void init();
void poll();
bool shutdownRequired();
void shutdownIfNeeded();

Status      status();
ChargeLevel level();
bool        isCharging();
bool        isUsbAvailable();
float       voltage();
void        notePowerKeyPressed(uint32_t now);
bool        powerKeyIndicatorActive();
bool        consumePowerKeyTopBarFullRedraw();

float halReadBatteryVoltage();
bool  halReadUsbAvailable();
} // namespace BattTracker
