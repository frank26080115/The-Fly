#include <Arduino.h>
#include <M5Unified.h>

#include "AudioFileRecorder.h"
#include "AudioManager.h"
#include "ClockAgent.h"
#include "Display.h"
#include "utilfuncs.h"

namespace
{

constexpr const char* TAG             = "test_micrec";
constexpr uint32_t    kStatusReportMs = 500;
constexpr uint32_t    kButtonPollMs   = 5;
constexpr uint32_t    kCore0StackSize = 8192;
constexpr UBaseType_t kCore0Priority  = 2;

volatile bool g_stop_requested = false;
TaskHandle_t  g_core0_task     = nullptr;
uint16_t      g_cur_colour     = TFT_BLACK;
uint16_t      g_next_colour    = TFT_BLACK;

void request_stop(const char* reason)
{
    if (!g_stop_requested)
    {
        Serial.printf("%s: stop requested: %s\n", TAG, reason);
    }
    g_next_colour = TFT_GREEN;
    g_stop_requested = true;
}

bool switch_to_mic_mode()
{
    if (AudioManager::mode() == AudioManager::P2TMode::Mic)
    {
        return true;
    }

    Serial.printf("%s: push-to-talk down, enabling mic mode\n", TAG);
    if (!AudioManager::enableMicMode())
    {
        Serial.printf("%s: enableMicMode failed\n", TAG);
        request_stop("enableMicMode failed");
        return false;
    }
    return true;
}

bool switch_to_speaker_mode()
{
    if (AudioManager::mode() == AudioManager::P2TMode::Speaker)
    {
        return true;
    }

    Serial.printf("%s: push-to-talk released, enabling speaker mode\n", TAG);
    if (!AudioManager::enableSpeakerMode())
    {
        Serial.printf("%s: enableSpeakerMode failed\n", TAG);
        request_stop("enableSpeakerMode failed");
        return false;
    }
    return true;
}

void micrec_core0_task(void*)
{
    bool ptt_was_pressed = false;

    Serial.printf("%s: core 0 control task started\n", TAG);
    while (!g_stop_requested)
    {
        M5.update();

        if (M5.BtnC.wasPressed())
        {
            request_stop("right touch button");
            break;
        }

        const bool ptt_pressed = M5.BtnA.isPressed();
        if (ptt_pressed != ptt_was_pressed)
        {
            if (ptt_pressed)
            {
                switch_to_mic_mode();
                g_next_colour = TFT_RED;
            }
            else
            {
                switch_to_speaker_mode();
                g_next_colour = TFT_BLUE;
            }
            ptt_was_pressed = ptt_pressed;
        }

        if (AudioManager::mode() == AudioManager::P2TMode::Mic)
        {
            AudioManager::pump_mic2bt();
        }

        delay(kButtonPollMs);
    }

    Serial.printf("%s: core 0 control task idle\n", TAG);
    idle_forever();
}

void print_status()
{
    Serial.printf("%s: mode=%u mic_peak=%u mic_scaled_peak=%u file_bytes=%llu write_avg_ms=%.3f write_max_ms=%.3f\n",
                  TAG,
                  static_cast<unsigned>(AudioManager::mode()),
                  static_cast<unsigned>(AudioManager::micPeakLevel()),
                  static_cast<unsigned>(AudioManager::micScaledPeakLevel()),
                  static_cast<unsigned long long>(AudioFileRecorder::bytesWritten()),
                  AudioFileRecorder::writeDurationAverageMs(),
                  AudioFileRecorder::writeDurationMaxMs());
    AudioFileRecorder::resetWriteDurationStats();
}

float elapsed_ms(uint32_t start_us, uint32_t end_us)
{
    return static_cast<float>(end_us - start_us) / 1000.0f;
}

} // namespace

void test_micrec()
{
    Serial.begin(115200);
    delay(1000);

    auto cfg = M5.config();
    M5.begin(cfg);
    Clock.syncToCompileTime();

    thefly_display.setBrightness(255);
    thefly_display.setColorDepth(16);
    thefly_display.fillScreen(g_cur_colour);

    Serial.println();
    Serial.printf("%s: starting mic recording test\n", TAG);
    Serial.printf("%s: left touch button = push-to-talk, right touch button = stop\n", TAG);

    const uint32_t audio_init_start_us = micros();
    const bool     audio_init_ok       = AudioManager::init(AudioManager::Hardware::M5StackInternal);
    const uint32_t audio_init_end_us   = micros();
    Serial.printf("%s: AudioManager init took %.3f ms\n", TAG, elapsed_ms(audio_init_start_us, audio_init_end_us));
    if (!audio_init_ok)
    {
        Serial.printf("%s: AudioManager init failed\n", TAG);
        idle_forever();
    }

    M5.update();
    M5.BtnA.setDebounceThresh(20);
    M5.BtnB.setDebounceThresh(20);
    M5.BtnC.setDebounceThresh(20);

    if (!AudioManager::enableSpeakerMode())
    {
        Serial.printf("%s: initial speaker mode failed\n", TAG);
        idle_forever();
    }

    AudioFileRecorder::setPurePcmMode(false);
    AudioFileRecorder::resetWriteDurationStats();
    const uint32_t recorder_start_us = micros();
    const bool     recorder_started  = AudioFileRecorder::startRecording(AudioFileRecorder::RecordingType::Memo);
    const uint32_t recorder_end_us   = micros();
    Serial.printf("%s: startRecording took %.3f ms\n", TAG, elapsed_ms(recorder_start_us, recorder_end_us));
    if (!recorder_started)
    {
        Serial.printf("%s: recording start failed\n", TAG);
        idle_forever();
    }

    Serial.printf("%s: recording path: %s\n", TAG, AudioFileRecorder::currentSdPath());
    Serial.printf("%s: choking mic-to-Bluetooth FIFO for local recording test\n", TAG);
    AudioManager::micToBluetoothFifo().setChoked(true);

    // indicates ready
    g_cur_colour = TFT_BLUE;
    g_next_colour = TFT_BLUE;
    thefly_display.fillScreen(g_cur_colour);

    g_stop_requested = false;
    const BaseType_t task_created = xTaskCreatePinnedToCore(micrec_core0_task,
                                                            "micrec_core0",
                                                            kCore0StackSize,
                                                            nullptr,
                                                            kCore0Priority,
                                                            &g_core0_task,
                                                            0);
    if (task_created != pdPASS)
    {
        Serial.printf("%s: failed to create core 0 task\n", TAG);
        AudioManager::micToBluetoothFifo().setChoked(false);
        AudioFileRecorder::stopRecording(true);
        idle_forever();
    }

    uint32_t next_status_ms = millis() + kStatusReportMs;
    while (!g_stop_requested)
    {
        if (g_cur_colour != g_next_colour) {
            g_cur_colour = g_next_colour;
            thefly_display.fillScreen(g_cur_colour);
        }

        AudioFileRecorder::pump();

        const uint32_t now_ms = millis();
        if (static_cast<int32_t>(now_ms - next_status_ms) >= 0)
        {
            print_status();
            next_status_ms = now_ms + kStatusReportMs;
        }

        taskYIELD();
    }

    Serial.printf("%s: stopping recording\n", TAG);
    AudioManager::micToBluetoothFifo().setChoked(false);
    AudioManager::stop();
    if (!AudioFileRecorder::stopRecording())
    {
        Serial.printf("%s: recording stop failed\n", TAG);
        idle_forever();
    }

    thefly_display.fillScreen(TFT_GREEN);

    Serial.printf("%s: recording complete: %s bytes=%llu\n",
                  TAG,
                  AudioFileRecorder::currentSdPath(),
                  static_cast<unsigned long long>(AudioFileRecorder::bytesWritten()));

    idle_forever();
}
