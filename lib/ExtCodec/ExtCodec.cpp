// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "ExtCodec.h"

#include <Arduino.h>
#include <M5Unified.h>

#include <atomic>
#include <mutex>

#include "control_sgtl5000.h"
#include "driver/ledc.h"
#include "esp_err.h"
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
constexpr uint32_t kAdcPollIntervalMs           = 80;
constexpr uint32_t kAdcPollFastIntervalMs       = 5;
constexpr uint16_t kAdcSamplesPerPoll           = 4;
constexpr uint32_t kAdcTaskStackBytes           = 2048;
constexpr UBaseType_t kAdcTaskPriority          = 1;
constexpr float    kDefaultHeadphoneVolume      = 0.45f;
constexpr uint8_t  kDefaultLineInLevel          = 5;
constexpr uint8_t  kDefaultDedicatedMicGainDb   = 30;
constexpr uint32_t kLedcMclkFrequencyHz         = 8192000;

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
State    read_adc_state_sample();
void     apply_adc_state_sample(State sample);
void     reset_adc_state_samples();
void     set_state(State next);
void     set_event_bits(EventBits_t bits);
bool     report_ledc_result(const char* step, esp_err_t err);

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

bool start_ledc_mclk()
{
    ledc_timer_config_t timer = {};
    timer.speed_mode          = LEDC_HIGH_SPEED_MODE;
    timer.duty_resolution     = LEDC_TIMER_1_BIT;
    timer.timer_num           = LEDC_TIMER_0;
    timer.freq_hz             = kLedcMclkFrequencyHz;
    timer.clk_cfg             = LEDC_USE_APB_CLK;

    esp_err_t err = ledc_timer_config(&timer);
    if (!report_ledc_result("timer", err))
    {
        return false;
    }

    ledc_channel_config_t channel = {};
    channel.gpio_num              = kLedcMclkGpio;
    channel.speed_mode            = LEDC_HIGH_SPEED_MODE;
    channel.channel               = LEDC_CHANNEL_0;
    channel.intr_type             = LEDC_INTR_DISABLE;
    channel.timer_sel             = LEDC_TIMER_0;
    channel.duty                  = 1;
    channel.hpoint                = 0;

    err = ledc_channel_config(&channel);
    if (!report_ledc_result("channel", err))
    {
        return false;
    }

    DBG_LOGI(TAG,
             "LEDC MCLK started: GPIO%d target=%lu Hz duty=50%% resolution=1-bit",
             kLedcMclkGpio,
             static_cast<unsigned long>(kLedcMclkFrequencyHz));
    return true;
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
    if (kSGTL5000I2sMclk < 0)
    {
        DBG_LOGE(TAG, "SGTL5000 SYS_MCLK is not configured; this codec cannot derive SYS_MCLK from I2S BCLK");
        return false;
    }

    return g_codec.configureFor16k16BitStereo() && g_codec.inputSelect(AUDIO_INPUT_LINEIN) &&
           g_codec.lineInLevel(kDefaultLineInLevel, kDefaultLineInLevel) &&
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
    uint32_t t = millis() / 10000;
    t %= 3;
    switch (t)
    {
    case 0:
        set_state(EXTCODEC_NO_EARBUD);
        break;
    case 1:
        set_state(EXTCODEC_YES_EARBUD_WITH_MIC);
        break;
    case 2:
        set_state(EXTCODEC_YES_EARBUD);
        break;
    default:
        set_state(EXTCODEC_NO_EARBUD);
        break;
    }
    #elif defined(TEST_TEENSY_AUDIO_BRD)
    set_state(EXTCODEC_YES_EARBUD_WITH_MIC);
    #else
    if (!available())
    {
        reset_adc_state_samples();
        set_state(EXTCODEC_UNAVAIL);
    }
    else
    {
        for (uint16_t i = 0; i < kAdcSamplesPerPoll; ++i)
        {
            apply_adc_state_sample(read_adc_state_sample());
            if (i + 1 < kAdcSamplesPerPoll)
            {
                vTaskDelay(pdMS_TO_TICKS(kAdcPollFastIntervalMs));
            }
        }
    }
    #endif
}

State read_adc_state_sample()
{
    const uint16_t earbud_sense     = analogRead(kExtCodecEarbudSenseAdc);
    const uint16_t inline_mic_sense = analogRead(kExtCodecInlineMicSenseAdc);
    g_earbud_sense_raw.store(earbud_sense);
    g_inline_mic_sense_raw.store(inline_mic_sense);

    if (earbud_sense < kEarbudConnectedThreshold)
    {
        return EXTCODEC_NO_EARBUD;
    }
    if (inline_mic_sense > kInlineMicPresentThreshold)
    {
        return EXTCODEC_YES_EARBUD_WITH_MIC;
    }
    return EXTCODEC_YES_EARBUD;
}

void apply_adc_state_sample(State sample)
{
    static State    pending_state = EXTCODEC_UNAVAIL;
    static uint16_t pending_count = 0;

    if (sample == state())
    {
        pending_state = EXTCODEC_UNAVAIL;
        pending_count = 0;
        return;
    }

    if (sample != pending_state)
    {
        pending_state = sample;
        pending_count = 1;
        return;
    }

    ++pending_count;
    if (pending_count >= kAdcSamplesPerPoll)
    {
        set_state(sample);
        pending_state = EXTCODEC_UNAVAIL;
        pending_count = 0;
    }
}

void reset_adc_state_samples()
{
    apply_adc_state_sample(state());
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

void set_event_bits(EventBits_t bits)
{
    if (g_events)
    {
        xEventGroupSetBits(g_events, bits);
    }
}

bool report_ledc_result(const char* step, esp_err_t err)
{
    DBG_LOGI(TAG, "LEDC MCLK %s: %s", step, esp_err_to_name(err));
    return err == ESP_OK;
}

} // namespace
} // namespace ExtCodec
