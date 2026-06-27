#include <Arduino.h>
#include <M5Unified.h>

#include <stdarg.h>

#include "AudioFileRecorder.h"
#include "AudioManager.h"
#include "ClockAgent.h"
#include "ExtCodec.h"
#include "MicGainManager.h"
#include "MicroSdCard.h"
#include "conf.h"
#include "control_sgtl5000.h"
#include "utilfuncs.h"

namespace
{

constexpr const char* TAG = "test_allmicgains";

constexpr char     kRecordingTypeCode    = 'G';
constexpr uint8_t  kFirstMicGainDb       = 0;
constexpr uint8_t  kLastMicGainDb        = 63;
constexpr uint32_t kLeadSilenceMs        = 500;
constexpr uint32_t kLiveAudioMs          = 4000;
constexpr uint32_t kTrailSilenceMs       = 500;
constexpr uint32_t kStatusReportPeriodMs = 1000;
constexpr uint32_t kCore0PumpDelayMs     = 1;
constexpr uint32_t kCore0StackSize       = 8192;
constexpr UBaseType_t kCore0Priority     = 2;

TaskHandle_t  g_core0_task         = nullptr;
volatile bool g_audio_pump_running = false;

void logf(const char* format, ...)
{
    Serial.printf("%s[%lu]: ", TAG, static_cast<unsigned long>(millis()));

    va_list args;
    va_start(args, format);
    Serial.vprintf(format, args);
    va_end(args);

    Serial.println();
}

void print_datetime(const char* label, const m5::rtc_datetime_t& datetime)
{
    logf("%s %04d-%02d-%02d %02d:%02d:%02d",
         label ? label : "time",
         datetime.date.year,
         datetime.date.month,
         datetime.date.date,
         datetime.time.hours,
         datetime.time.minutes,
         datetime.time.seconds);
}

void stop_audio_pump_task()
{
    if (!g_core0_task)
    {
        return;
    }

    g_audio_pump_running = false;
    for (uint8_t i = 0; i < 50 && g_core0_task; ++i)
    {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void halt_test(const char* message)
{
    if (message && message[0] != '\0')
    {
        logf("HALT %s", message);
    }

    stop_audio_pump_task();
    AudioManager::setMicMuted(true);
    AudioManager::micToBluetoothFifo().setChoked(false);
    AudioManager::stop();
    if (AudioFileRecorder::isRecording())
    {
        AudioFileRecorder::stopRecording(true);
    }
    Serial.flush();
    idle_forever();
}

void audio_pump_task(void*)
{
    logf("core 0 mic pump started");
    while (g_audio_pump_running)
    {
        AudioManager::pump_mic2bt();
        vTaskDelay(pdMS_TO_TICKS(kCore0PumpDelayMs));
    }

    logf("core 0 mic pump stopped");
    g_core0_task = nullptr;
    vTaskDelete(nullptr);
}

bool start_audio_pump_task()
{
    if (g_core0_task)
    {
        return true;
    }

    g_audio_pump_running = true;
    const BaseType_t created = xTaskCreatePinnedToCore(audio_pump_task,
                                                       "allmicgains_audio",
                                                       kCore0StackSize,
                                                       nullptr,
                                                       kCore0Priority,
                                                       &g_core0_task,
                                                       0);
    if (created == pdPASS)
    {
        return true;
    }

    g_audio_pump_running = false;
    g_core0_task         = nullptr;
    return false;
}

bool wait_for_inline_mic(uint32_t timeout_ms)
{
    const uint32_t started_ms = millis();
    while (!ExtCodec::inlineMicPresent() && static_cast<uint32_t>(millis() - started_ms) < timeout_ms)
    {
        ExtCodec::waitForEvents(ExtCodec::kStateChangedEvent, pdMS_TO_TICKS(80));
    }

    return ExtCodec::inlineMicPresent();
}

bool set_mic_gain(uint8_t gain_db)
{
    AudioControlSGTL5000* codec = ExtCodec::control();
    if (!codec)
    {
        logf("SGTL5000 control unavailable");
        return false;
    }

    const uint32_t started_us = micros();
    const bool     ok         = codec->micGain(gain_db);
    const float    elapsed_ms = static_cast<float>(micros() - started_us) / 1000.0f;
    logf("micGain(%u dB) %s elapsed=%.3f ms",
         static_cast<unsigned>(gain_db),
         ok ? "ok" : "FAILED",
         static_cast<double>(elapsed_ms));
    return ok;
}

void print_phase_status(uint8_t gain_db, const char* phase, uint32_t elapsed_ms, uint32_t duration_ms)
{
    logf("gain=%u dB phase=%s elapsed=%lu/%lu ms file_bytes=%llu mic_fifo=%u%% host_fifo=%u%%",
         static_cast<unsigned>(gain_db),
         phase ? phase : "?",
         static_cast<unsigned long>(elapsed_ms),
         static_cast<unsigned long>(duration_ms),
         static_cast<unsigned long long>(AudioFileRecorder::bytesWritten()),
         static_cast<unsigned>(AudioManager::micToFileFifo().getFillPercentage()),
         static_cast<unsigned>(AudioManager::bluetoothToFileFifo().getFillPercentage()));
}

void record_phase(uint8_t gain_db, const char* phase, uint32_t duration_ms, bool muted)
{
    AudioManager::setMicMuted(muted);
    logf("gain=%u dB phase=%s start muted=%u duration=%lu ms",
         static_cast<unsigned>(gain_db),
         phase ? phase : "?",
         muted ? 1U : 0U,
         static_cast<unsigned long>(duration_ms));

    const uint32_t started_ms     = millis();
    uint32_t       next_report_ms = started_ms;
    while (static_cast<uint32_t>(millis() - started_ms) < duration_ms)
    {
        AudioFileRecorder::pump();

        const uint32_t now_ms = millis();
        if (static_cast<int32_t>(now_ms - next_report_ms) >= 0)
        {
            print_phase_status(gain_db, phase, now_ms - started_ms, duration_ms);
            next_report_ms = now_ms + kStatusReportPeriodMs;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    AudioFileRecorder::pump();
    print_phase_status(gain_db, phase, duration_ms, duration_ms);
}

void setup_file_fifos_for_mic_recording()
{
    AudioManager::bluetoothToFileFifo().clear();
    AudioManager::micToFileFifo().clear();
    AudioManager::micToBluetoothFifo().clear();
    AudioManager::micToBluetoothFifo().setChoked(true);
    AudioManager::setMicMuted(true);
}

void setup_hardware()
{
    const bool clock_ok = Clock.begin();
    m5::rtc_datetime_t rtc_datetime = {};
    const bool          rtc_ok       = M5.Rtc.getDateTime(&rtc_datetime);
    logf("RTC clock_begin=%u rtc_read=%u rtc_enabled=%u",
         clock_ok ? 1U : 0U,
         rtc_ok ? 1U : 0U,
         M5.Rtc.isEnabled() ? 1U : 0U);
    if (rtc_ok)
    {
        print_datetime("RTC", rtc_datetime);
    }

    const bool sd_ok = MicroSdCard::begin();
    logf("microSD begin=%u ready=%u free=%llu",
         sd_ok ? 1U : 0U,
         MicroSdCard::isReady() ? 1U : 0U,
         static_cast<unsigned long long>(sd_ok ? MicroSdCard::freeBytes() : 0));
    if (!sd_ok)
    {
        halt_test("microSD init failed");
    }

    const bool i2s_ok   = AudioManager::configure_i2s_shared();
    const bool codec_ok = ExtCodec::init();
    logf("ExtCodec init i2s=%u codec=%u initialized=%u available=%u state=%s earbud_adc=%u inline_mic_adc=%u pll=%u",
         i2s_ok ? 1U : 0U,
         codec_ok ? 1U : 0U,
         ExtCodec::initialized() ? 1U : 0U,
         ExtCodec::available() ? 1U : 0U,
         ExtCodec::stateName(ExtCodec::state()),
         static_cast<unsigned>(ExtCodec::earbudSenseRaw()),
         static_cast<unsigned>(ExtCodec::inlineMicSenseRaw()),
         ExtCodec::pllLocked() ? 1U : 0U);
    if (!i2s_ok || !codec_ok || !ExtCodec::available())
    {
        halt_test("external codec init failed");
    }

    if (!wait_for_inline_mic(1000))
    {
        logf("inline mic not detected: state=%s earbud_adc=%u inline_mic_adc=%u",
             ExtCodec::stateName(ExtCodec::state()),
             static_cast<unsigned>(ExtCodec::earbudSenseRaw()),
             static_cast<unsigned>(ExtCodec::inlineMicSenseRaw()));
        halt_test("inline earbud mic is required");
    }

    const bool audio_ok = AudioManager::init(AudioManager::Hardware::ExternalI2SCodec);
    AudioManager::forceExternalHeadphoneForExternalCodec(false);
    AudioManager::forceInternalSpeakerForExternalCodec(false);
    AudioManager::setExternalCodecMicOverride(AudioManager::ExternalCodecMicOverride::DedicatedMic);
    setup_file_fifos_for_mic_recording();

    const bool mic_ok = audio_ok && AudioManager::enableMicMode();
    AudioManager::setMicMuted(true);
    MicGainManager::setBypass(true);

    logf("AudioManager init audio=%u mic=%u mode=%u forced_input=dedicated-mic software_mic_gain_bypass=1",
         audio_ok ? 1U : 0U,
         mic_ok ? 1U : 0U,
         static_cast<unsigned>(AudioManager::mode()));
    if (!audio_ok || !mic_ok)
    {
        halt_test("AudioManager mic setup failed");
    }
}

} // namespace

void test_allmicgains()
{
    Serial.begin(115200, SERIAL_8N1, -1, 1);
    delay(250);

    Serial.println();
    logf("starting SGTL5000 inline mic gain sweep");
    logf("gain range=%u..%u dB lead_silence=%lu ms live=%lu ms trail_silence=%lu ms",
         static_cast<unsigned>(kFirstMicGainDb),
         static_cast<unsigned>(kLastMicGainDb),
         static_cast<unsigned long>(kLeadSilenceMs),
         static_cast<unsigned long>(kLiveAudioMs),
         static_cast<unsigned long>(kTrailSilenceMs));

#if defined(BUILD_USE_MP3_COMPRESSION)
    logf("warning: BUILD_USE_MP3_COMPRESSION is enabled; this test was requested for WAV recording");
#elif BUILD_WITH_SECURITY_LEVEL >= 1
    logf("warning: BUILD_WITH_SECURITY_LEVEL=%d; output will be encrypted WAV container, not plain .wav",
         BUILD_WITH_SECURITY_LEVEL);
#else
    logf("recording format: plain WAV PCM");
#endif

    setup_hardware();

    AudioFileRecorder::setPurePcmMode(false);
    AudioFileRecorder::resetWriteDurationStats();
    if (!start_audio_pump_task())
    {
        halt_test("audio pump task failed");
    }

    if (!AudioFileRecorder::startRecording(kRecordingTypeCode))
    {
        halt_test("recording start failed");
    }

    logf("recording path: %s", AudioFileRecorder::currentSdPath());

    for (uint16_t gain = kFirstMicGainDb; gain <= kLastMicGainDb; ++gain)
    {
        const uint8_t gain_db = static_cast<uint8_t>(gain);
        AudioManager::setMicMuted(true);
        if (!set_mic_gain(gain_db))
        {
            halt_test("SGTL5000 mic gain write failed");
        }

        record_phase(gain_db, "lead-silence", kLeadSilenceMs, true);
        record_phase(gain_db, "live", kLiveAudioMs, false);
        record_phase(gain_db, "trail-silence", kTrailSilenceMs, true);
    }

    AudioManager::setMicMuted(true);
    for (uint8_t i = 0; i < 10; ++i)
    {
        AudioFileRecorder::pump();
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    stop_audio_pump_task();
    AudioManager::stop();
    if (!AudioFileRecorder::stopRecording())
    {
        halt_test("recording stop failed");
    }

    logf("recording complete: %s bytes=%llu write_avg_ms=%.3f write_max_ms=%.3f fifo_overflows=%lu",
         AudioFileRecorder::currentSdPath(),
         static_cast<unsigned long long>(AudioFileRecorder::bytesWritten()),
         static_cast<double>(AudioFileRecorder::writeDurationAverageMs()),
         static_cast<double>(AudioFileRecorder::writeDurationMaxMs()),
         static_cast<unsigned long>(AudioFileRecorder::fifoOverflowEvents()));
    logf("SGTL5000 inline mic gain sweep finished");

    AudioManager::micToBluetoothFifo().setChoked(false);
    idle_forever();
}
