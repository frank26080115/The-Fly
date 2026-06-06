// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "ExtCodec.h"

#include <Arduino.h>
#include <M5Unified.h>

#include <atomic>
#include <mutex>

#include "control_sgtl5000.h"
#include "pins.h"

namespace ExtCodec
{
namespace
{

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

constexpr const char* TAG = "ExtCodec";

constexpr uint8_t kSGTL5000I2cAddress = 0x0A;
constexpr uint32_t kSGTL5000I2cFreq    = 400000;

constexpr uint16_t kEarbudConnectedThreshold    = 1800;
constexpr uint16_t kInlineMicPresentThreshold   = 800;
constexpr uint32_t kAdcPollIntervalMs           = 100;
constexpr uint16_t kAdcSamplesPerPoll           = 4;
constexpr uint32_t kAdcTaskStackBytes           = 2048;
constexpr UBaseType_t kAdcTaskPriority          = 1;
constexpr float    kDefaultHeadphoneVolume      = 0.45f;
constexpr uint8_t  kDefaultLineInLevel          = 5;
constexpr uint8_t  kDefaultDedicatedMicGainDb   = 30;

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

AudioControlSGTL5000 g_codec;
std::mutex           g_codec_mutex;
TaskHandle_t         g_adc_task = nullptr;
EventGroupHandle_t   g_events   = nullptr;

std::atomic<bool>     g_initialized{false};
std::atomic<bool>     g_available{false};
std::atomic<State>    g_state{EXTCODEC_UNAVAIL};
std::atomic<uint32_t> g_state_generation{0};
std::atomic<uint16_t> g_earbud_sense_raw{0};
std::atomic<uint16_t> g_inline_mic_sense_raw{0};

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

bool     begin_i2c();
bool     sgtl5000_address_responds();
bool     begin_sgtl5000_control();
bool     start_adc_task();
void     adc_task(void*);
void     poll_adc_state();
State    state_from_adc(uint16_t earbud_sense, uint16_t inline_mic_sense);
void     set_state(State next);
uint16_t read_adc_average(int pin);
void     set_event_bits(EventBits_t bits);

} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

bool init()
{
    if (g_initialized.exchange(true))
    {
        return g_available.load();
    }

    g_events = xEventGroupCreate();

    #ifndef TEST_MOCK_EXT_CODEC
    pinMode(kExtCodecEarbudSenseAdc, INPUT);
    pinMode(kExtCodecInlineMicSenseAdc, INPUT);

    if (!begin_i2c() || !sgtl5000_address_responds())
    {
        g_available.store(false);
        set_state(EXTCODEC_UNAVAIL);
        DBG_LOGI(TAG, "SGTL5000 unavailable at I2C address 0x%02X", kSGTL5000I2cAddress);
        return false;
    }

    if (!begin_sgtl5000_control())
    {
        g_available.store(false);
        set_state(EXTCODEC_UNAVAIL);
        DBG_LOGW(TAG, "SGTL5000 responded, but codec setup failed");
        return false;
    }

    g_available.store(true);
    poll_adc_state();
    if (!start_adc_task())
    {
        g_available.store(false);
        set_state(EXTCODEC_UNAVAIL);
        DBG_LOGE(TAG, "SGTL5000 detected, but ADC monitor task could not start");
        return false;
    }

    DBG_LOGI(TAG, "SGTL5000 detected: state=%s", stateName(state()));
    return true;
    #else
    g_available.store(true);
    poll_adc_state();
    if (!start_adc_task())
    {
        g_available.store(false);
        set_state(EXTCODEC_UNAVAIL);
        DBG_LOGE(TAG, "mock SGTL5000 enabled, but state task could not start");
        return false;
    }

    DBG_LOGI(TAG, "mock SGTL5000 enabled: state=%s", stateName(state()));
    return true;
    #endif
}

bool initialized()
{
    return g_initialized.load();
}

bool available()
{
    return g_available.load();
}

State state()
{
    return g_state.load();
}

uint32_t stateGeneration()
{
    return g_state_generation.load();
}

const char* stateName(State value)
{
    switch (value)
    {
    case EXTCODEC_UNAVAIL:
        return "EXTCODEC_UNAVAIL";
    case EXTCODEC_NO_EARBUD:
        return "EXTCODEC_NO_EARBUD";
    case EXTCODEC_YES_EARBUD:
        return "EXTCODEC_YES_EARBUD";
    case EXTCODEC_YES_EARBUD_WITH_MIC:
        return "EXTCODEC_YES_EARBUD_WITH_MIC";
    default:
        return "UNKNOWN";
    }
}

// -----------------------------------------------------------------------------
// Feature Logic
// -----------------------------------------------------------------------------

bool earbudPresent()
{
    const State current = state();
    return current == EXTCODEC_YES_EARBUD || current == EXTCODEC_YES_EARBUD_WITH_MIC;
}

bool inlineMicPresent()
{
    return state() == EXTCODEC_YES_EARBUD_WITH_MIC;
}

bool fullDuplexAvailable()
{
    return earbudPresent();
}

bool pushToTalkRequired()
{
    const State current = state();
    return current == EXTCODEC_UNAVAIL || current == EXTCODEC_NO_EARBUD;
}

MicInput micInputForState(State value)
{
    return value == EXTCODEC_YES_EARBUD_WITH_MIC ? MicInput::DedicatedMic : MicInput::LineInRight;
}

const char* micInputName(MicInput value)
{
    switch (value)
    {
    case MicInput::LineInRight:
        return "line-in-right";
    case MicInput::DedicatedMic:
        return "dedicated-mic";
    default:
        return "unknown";
    }
}

bool configureAnalogPathForState(State value)
{
    #ifndef TEST_MOCK_EXT_CODEC
    if (!available())
    {
        return false;
    }

    const MicInput input = micInputForState(value);
    std::lock_guard<std::mutex> lock(g_codec_mutex);
    if (input == MicInput::DedicatedMic)
    {
        return g_codec.inputSelect(AUDIO_INPUT_MIC) && g_codec.micGain(kDefaultDedicatedMicGainDb);
    }

    return g_codec.inputSelect(AUDIO_INPUT_LINEIN) && g_codec.lineInLevel(kDefaultLineInLevel, kDefaultLineInLevel);
    #else
    return true;
    #endif
}

uint16_t earbudSenseRaw()
{
    #ifndef TEST_MOCK_EXT_CODEC
    return g_earbud_sense_raw.load();
    #else
    return earbudPresent() ? 2500 : 0;
    #endif
}

uint16_t inlineMicSenseRaw()
{
    #ifndef TEST_MOCK_EXT_CODEC
    return g_inline_mic_sense_raw.load();
    #else
    return inlineMicPresent() ? 1200 : 0;
    #endif
}

EventBits_t waitForEvents(EventBits_t bits, TickType_t ticksToWait)
{
    if (!g_events)
    {
        return 0;
    }
    return xEventGroupWaitBits(g_events, bits, pdTRUE, pdFALSE, ticksToWait);
}

EventBits_t takeEvents()
{
    if (!g_events)
    {
        return 0;
    }
    return xEventGroupClearBits(g_events, kStateChangedEvent);
}

AudioControlSGTL5000* control()
{
    return available() ? &g_codec : nullptr;
}

namespace
{

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

bool begin_i2c()
{
    if (M5.In_I2C.isEnabled())
    {
        return true;
    }

    DBG_LOGW(TAG, "internal I2C bus is not enabled");
    return false;
}

bool sgtl5000_address_responds()
{
    #ifndef TEST_MOCK_EXT_CODEC
    return M5.In_I2C.scanID(kSGTL5000I2cAddress, kSGTL5000I2cFreq);
    #else
    return true;
    #endif
}

bool begin_sgtl5000_control()
{
    #ifndef TEST_MOCK_EXT_CODEC
    std::lock_guard<std::mutex> lock(g_codec_mutex);
    g_codec.setAddress(LOW);
    if (!g_codec.enable())
    {
        return false;
    }

    return g_codec.inputSelect(AUDIO_INPUT_LINEIN) && g_codec.lineInLevel(kDefaultLineInLevel, kDefaultLineInLevel) &&
           g_codec.micGain(kDefaultDedicatedMicGainDb) && g_codec.volume(kDefaultHeadphoneVolume) &&
           g_codec.muteHeadphone();
    #else
    return true;
    #endif
}

bool start_adc_task()
{
    if (g_adc_task)
    {
        return true;
    }

    return xTaskCreate(adc_task, "ExtCodecAdc", kAdcTaskStackBytes, nullptr, kAdcTaskPriority, &g_adc_task) == pdPASS;
}

void adc_task(void*)
{
    while (true)
    {
        poll_adc_state();
        vTaskDelay(pdMS_TO_TICKS(kAdcPollIntervalMs));
    }
}

void poll_adc_state()
{
    #ifdef TEST_MOCK_EXT_CODEC
    set_state(state_from_adc(0, 0));
    #else
    const uint16_t earbud_sense    = read_adc_average(kExtCodecEarbudSenseAdc);
    const uint16_t inline_mic_sense = read_adc_average(kExtCodecInlineMicSenseAdc);

    g_earbud_sense_raw.store(earbud_sense);
    g_inline_mic_sense_raw.store(inline_mic_sense);
    set_state(state_from_adc(earbud_sense, inline_mic_sense));
    #endif
}

State state_from_adc(uint16_t earbud_sense, uint16_t inline_mic_sense)
{
    #ifndef TEST_MOCK_EXT_CODEC
    if (!available())
    {
        return EXTCODEC_UNAVAIL;
    }
    if (earbud_sense < kEarbudConnectedThreshold)
    {
        return EXTCODEC_NO_EARBUD;
    }
    if (inline_mic_sense > kInlineMicPresentThreshold)
    {
        return EXTCODEC_YES_EARBUD_WITH_MIC;
    }
    return EXTCODEC_YES_EARBUD;
    #else
    uint32_t t = millis() / 10000;
    t %= 3;
    switch (t)
    {
    case 0:
        return EXTCODEC_NO_EARBUD;
    case 1:
        return EXTCODEC_YES_EARBUD_WITH_MIC;
    case 2:
        return EXTCODEC_YES_EARBUD;
    default:
        return EXTCODEC_NO_EARBUD;
    }
    #endif
}

void set_state(State next)
{
    const State previous = g_state.exchange(next);
    if (previous == next)
    {
        return;
    }

    ++g_state_generation;
    set_event_bits(kStateChangedEvent);
    DBG_LOGI(TAG, "state changed: %s -> %s", stateName(previous), stateName(next));
}

uint16_t read_adc_average(int pin)
{
    uint32_t total = 0;
    for (uint16_t i = 0; i < kAdcSamplesPerPoll; ++i)
    {
        total += static_cast<uint32_t>(analogRead(pin));
    }
    return static_cast<uint16_t>(total / kAdcSamplesPerPoll);
}

void set_event_bits(EventBits_t bits)
{
    if (g_events)
    {
        xEventGroupSetBits(g_events, bits);
    }
}

} // namespace
} // namespace ExtCodec
