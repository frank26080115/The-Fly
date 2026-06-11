#pragma once

#include "thefly_common.h"

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <stdint.h>

class AudioControlSGTL5000;

namespace ExtCodec
{

enum State : uint8_t
{
    EXTCODEC_UNAVAIL,
    EXTCODEC_NO_EARBUD,
    EXTCODEC_YES_EARBUD,
    EXTCODEC_YES_EARBUD_WITH_MIC,
};

enum class MicInput : uint8_t
{
    LineInRight,
    DedicatedMic,
};

static constexpr EventBits_t kStateChangedEvent = BIT0;

bool init();
bool initialized();
bool available();

State    state();
uint32_t stateGeneration();
const char* stateName(State value);

bool earbudPresent();
bool inlineMicPresent();
bool fullDuplexAvailable();
bool pushToTalkRequired();
bool pllLocked();
bool readChipAnaStatus(uint16_t& status);
bool start_ledc_mclk();

MicInput    micInputForState(State value);
const char* micInputName(MicInput value);
bool        configureAnalogPathForState(State value);

uint16_t earbudSenseRaw();
uint16_t inlineMicSenseRaw();

EventBits_t waitForEvents(EventBits_t bits, TickType_t ticksToWait);
EventBits_t takeEvents();

AudioControlSGTL5000* control();

} // namespace ExtCodec
