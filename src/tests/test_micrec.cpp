#include <Arduino.h>
#include <M5Unified.h>

#include <stdarg.h>

#include "AudioFileRecorder.h"
#include "AudioManager.h"
#include "ClockAgent.h"
#include "Display.h"
#include "MicroSdCard.h"
#include "conf.h"
#include "defs.h"
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
bool          g_bt2spk_overflowed = false;
bool          g_bt2file_overflowed = false;
bool          g_mic2bt_overflowed = false;
bool          g_mic2file_overflowed = false;

void logf(const char* format, ...)
{
    Serial.printf("%s[%lu]: ", TAG, static_cast<unsigned long>(millis()));

    va_list args;
    va_start(args, format);
    Serial.vprintf(format, args);
    va_end(args);

    Serial.println();
}

void request_stop(const char* reason)
{
    if (!g_stop_requested)
    {
        logf("stop requested: %s", reason);
    }
    g_next_colour = TFT_GREEN;
    g_stop_requested = true;
}

void report_fifo_overflow_transition(const char* name, AudioFifo& fifo, bool& was_overflowed)
{
    const bool overflowed = fifo.overflowed();
    if (overflowed && !was_overflowed)
    {
        logf("FIFO overflow flag set: %s used=%u capacity=%u fill=%u%%",
             name,
             static_cast<unsigned>(fifo.usedSamples()),
             static_cast<unsigned>(fifo.capacity()),
             static_cast<unsigned>(fifo.getFillPercentage()));
    }

    was_overflowed = overflowed;
}

void report_fifo_overflow_transitions()
{
    report_fifo_overflow_transition("bluetooth_to_speaker", AudioManager::bluetoothToSpeakerFifo(), g_bt2spk_overflowed);
    report_fifo_overflow_transition("bluetooth_to_file", AudioManager::bluetoothToFileFifo(), g_bt2file_overflowed);
    report_fifo_overflow_transition("mic_to_bluetooth", AudioManager::micToBluetoothFifo(), g_mic2bt_overflowed);
    report_fifo_overflow_transition("mic_to_file", AudioManager::micToFileFifo(), g_mic2file_overflowed);
}

void reset_fifo_overflow_watchers()
{
    AudioManager::bluetoothToSpeakerFifo().resetOverflowFlag();
    AudioManager::bluetoothToFileFifo().resetOverflowFlag();
    AudioManager::micToBluetoothFifo().resetOverflowFlag();
    AudioManager::micToFileFifo().resetOverflowFlag();

    g_bt2spk_overflowed = false;
    g_bt2file_overflowed = false;
    g_mic2bt_overflowed = false;
    g_mic2file_overflowed = false;
}

bool switch_to_mic_mode()
{
    if (AudioManager::mode() == AudioManager::P2TMode::Mic)
    {
        return true;
    }

    logf("push-to-talk down, enabling mic mode");
    if (!AudioManager::enableMicMode())
    {
        logf("enableMicMode failed");
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

    logf("push-to-talk released, enabling speaker mode");
    if (!AudioManager::enableSpeakerMode())
    {
        logf("enableSpeakerMode failed");
        request_stop("enableSpeakerMode failed");
        return false;
    }
    return true;
}

void micrec_core0_task(void*)
{
    bool ptt_was_pressed = false;

    logf("core 0 control task started");
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
            report_fifo_overflow_transitions();
        }

        delay(kButtonPollMs);
    }

    logf("core 0 control task idle");
    idle_forever();
}

void print_status()
{
    logf("mode=%u mic_peak=%u mic_scaled_peak=%u file_bytes=%llu write_avg_ms=%.3f write_max_ms=%.3f",
         static_cast<unsigned>(AudioManager::mode()),
         static_cast<unsigned>(AudioManager::micPeakLevel()),
         static_cast<unsigned>(AudioManager::micScaledPeakLevel()),
         static_cast<unsigned long long>(AudioFileRecorder::bytesWritten()),
         AudioFileRecorder::writeDurationAverageMs(),
         AudioFileRecorder::writeDurationMaxMs());
    AudioFileRecorder::resetWriteDurationStats();
}

uint64_t estimate_duration_ms_from_pcm_bytes(uint64_t pcm_bytes)
{
    constexpr uint64_t bytes_per_second =
        static_cast<uint64_t>(AUDIO_RECORDER_SAMPLE_RATE_HZ) * AUDIO_RECORDER_FRAME_BYTES;
    return (pcm_bytes * 1000ULL + (bytes_per_second / 2ULL)) / bytes_per_second;
}

uint64_t encrypted_audio_payload_bytes(uint64_t encrypted_bytes, uint64_t encrypted_chunk_bytes, uint64_t plaintext_chunk_bytes)
{
    if (encrypted_chunk_bytes <= RECORDER_ENCRYPTED_CHUNK_OVERHEAD)
    {
        return 0;
    }

    const uint64_t full_chunks = encrypted_bytes / encrypted_chunk_bytes;
    const uint64_t remainder   = encrypted_bytes % encrypted_chunk_bytes;
    uint64_t payload_bytes     = full_chunks * plaintext_chunk_bytes;

    if (remainder > RECORDER_ENCRYPTED_CHUNK_OVERHEAD)
    {
        payload_bytes += remainder - RECORDER_ENCRYPTED_CHUNK_OVERHEAD;
    }

    return payload_bytes;
}

void print_recording_file_analysis(uint64_t file_bytes)
{
#if defined(BUILD_USE_MP3_COMPRESSION)
    #if BUILD_WITH_SECURITY_LEVEL >= 1
    const uint64_t mp3_payload_bytes = encrypted_audio_payload_bytes(file_bytes,
                                                                     MP3_ENCRYPTED_CHUNK_LENGTH,
                                                                     MP3_ENCRYPTED_PLAINTEXT_LENGTH);
    const uint64_t estimated_ms =
        (mp3_payload_bytes * 1000ULL + (MP3_CBR_BYTES_PER_SECOND / 2ULL)) / MP3_CBR_BYTES_PER_SECOND;
    logf("file analysis: format=encrypted-mp3 file_bytes=%llu mp3_payload_bytes=%llu estimated_duration_ms=%llu (%.3f seconds)",
         static_cast<unsigned long long>(file_bytes),
         static_cast<unsigned long long>(mp3_payload_bytes),
         static_cast<unsigned long long>(estimated_ms),
         static_cast<double>(estimated_ms) / 1000.0);
    #else
    const uint64_t estimated_ms =
        (file_bytes * 1000ULL + (MP3_CBR_BYTES_PER_SECOND / 2ULL)) / MP3_CBR_BYTES_PER_SECOND;
    const uint64_t whole_mp3_frames = file_bytes / MP3_CBR_BYTES_PER_MP3_FRAME;
    logf("file analysis: format=mp3-cbr file_bytes=%llu bitrate=%u kbps estimated_duration_ms=%llu (%.3f seconds) whole_mp3_frames=%llu remainder=%llu bytes",
         static_cast<unsigned long long>(file_bytes),
         static_cast<unsigned>(MP3_BITRATE_KBPS),
         static_cast<unsigned long long>(estimated_ms),
         static_cast<double>(estimated_ms) / 1000.0,
         static_cast<unsigned long long>(whole_mp3_frames),
         static_cast<unsigned long long>(file_bytes % MP3_CBR_BYTES_PER_MP3_FRAME));
    #endif
#else
    #if BUILD_WITH_SECURITY_LEVEL >= 1
    uint64_t encrypted_audio_bytes = 0;
    if (file_bytes > WAV_ENCRYPTED_RIFF_HEADER_LENGTH)
    {
        encrypted_audio_bytes = file_bytes - WAV_ENCRYPTED_RIFF_HEADER_LENGTH;
    }

    const uint64_t pcm_payload_bytes = encrypted_audio_payload_bytes(encrypted_audio_bytes,
                                                                     WAV_ENCRYPTED_AUDIO_CHUNK_LENGTH,
                                                                     WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH);
    const uint64_t estimated_ms = estimate_duration_ms_from_pcm_bytes(pcm_payload_bytes);
    logf("file analysis: format=encrypted-wav file_bytes=%llu pcm_payload_bytes=%llu estimated_duration_ms=%llu (%.3f seconds)",
         static_cast<unsigned long long>(file_bytes),
         static_cast<unsigned long long>(pcm_payload_bytes),
         static_cast<unsigned long long>(estimated_ms),
         static_cast<double>(estimated_ms) / 1000.0);
    #else
    uint64_t pcm_payload_bytes = 0;
    if (file_bytes > WAV_RIFF_HEADER_LENGTH)
    {
        pcm_payload_bytes = file_bytes - WAV_RIFF_HEADER_LENGTH;
    }

    const uint64_t estimated_ms = estimate_duration_ms_from_pcm_bytes(pcm_payload_bytes);
    logf("file analysis: format=wav-pcm file_bytes=%llu pcm_payload_bytes=%llu estimated_duration_ms=%llu (%.3f seconds)",
         static_cast<unsigned long long>(file_bytes),
         static_cast<unsigned long long>(pcm_payload_bytes),
         static_cast<unsigned long long>(estimated_ms),
         static_cast<double>(estimated_ms) / 1000.0);
    #endif
#endif
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

    logf("starting mic recording test");
    logf("left touch button = push-to-talk, right touch button = stop");

    const uint32_t audio_init_start_us = micros();
    const bool     audio_init_ok       = AudioManager::init(AudioManager::Hardware::M5StackInternal);
    const uint32_t audio_init_end_us   = micros();
    logf("AudioManager init took %.3f ms", elapsed_ms(audio_init_start_us, audio_init_end_us));
    if (!audio_init_ok)
    {
        logf("AudioManager init failed");
        idle_forever();
    }

    reset_fifo_overflow_watchers();

    M5.update();
    M5.BtnA.setDebounceThresh(20);
    M5.BtnB.setDebounceThresh(20);
    M5.BtnC.setDebounceThresh(20);

    if (!AudioManager::enableSpeakerMode())
    {
        logf("initial speaker mode failed");
        idle_forever();
    }

    const uint32_t microsd_start_us = micros();
    const bool     microsd_ok       = MicroSdCard::begin();
    const uint32_t microsd_end_us   = micros();
    logf("MicroSdCard init took %.3f ms", elapsed_ms(microsd_start_us, microsd_end_us));
    if (!microsd_ok)
    {
        logf("MicroSdCard init failed");
        idle_forever();
    }

    AudioFileRecorder::setPurePcmMode(false);
    AudioFileRecorder::resetWriteDurationStats();
    const uint32_t recorder_start_us = micros();
    const bool     recorder_started  = AudioFileRecorder::startRecording(AudioFileRecorder::RecordingType::Memo);
    const uint32_t recorder_end_us   = micros();
    logf("startRecording took %.3f ms", elapsed_ms(recorder_start_us, recorder_end_us));
    if (!recorder_started)
    {
        logf("recording start failed");
        idle_forever();
    }

    logf("recording path: %s", AudioFileRecorder::currentSdPath());
    logf("choking mic-to-Bluetooth FIFO for local recording test");
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
        logf("failed to create core 0 task");
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

        report_fifo_overflow_transitions();
        AudioFileRecorder::pump();
        report_fifo_overflow_transitions();

        const uint32_t now_ms = millis();
        if (static_cast<int32_t>(now_ms - next_status_ms) >= 0)
        {
            print_status();
            next_status_ms = now_ms + kStatusReportMs;
        }

        taskYIELD();
    }

    logf("stopping recording");
    AudioManager::micToBluetoothFifo().setChoked(false);
    AudioManager::stop();
    if (!AudioFileRecorder::stopRecording())
    {
        logf("recording stop failed");
        idle_forever();
    }

    thefly_display.fillScreen(TFT_GREEN);

    const uint64_t final_bytes = AudioFileRecorder::bytesWritten();
    logf("recording complete: %s bytes=%llu",
         AudioFileRecorder::currentSdPath(),
         static_cast<unsigned long long>(final_bytes));
    print_recording_file_analysis(final_bytes);

    idle_forever();
}
