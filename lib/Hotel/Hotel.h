#pragma once

#include "thefly_common.h"

#include <stdint.h>

namespace Hotel
{

enum class State : uint8_t
{
    Blocked,
    RecentlyActive,
    ActiveDimAllowed,
    LightSleepReady,
    DimLightSleepReady,
    Shutdown,
};

void init();

void pollCore0();
void pollCore1();

void noteUserActivity();
void noteUserActivityFromIsr();

State       state();
const char* stateName(State value);

} // namespace Hotel
