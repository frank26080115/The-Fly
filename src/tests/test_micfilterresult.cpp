#include <Arduino.h>
#include <math.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string.h>

#include "AudioFileRecorder.h"
#include "AudioManager.h"
#include "ClockAgent.h"
#include "MicGainManager.h"
#include "utilfuncs.h"

namespace
{

constexpr const char* TAG = "test_micfilterresult";

constexpr uint32_t kSampleRateHz      = AudioManager::kSampleRateHz;
constexpr uint32_t kSegmentSeconds    = 10;
constexpr uint32_t kSegmentSamples    = kSampleRateHz * kSegmentSeconds;
constexpr uint32_t kTotalSamples      = kSegmentSamples * 2U;
constexpr size_t   kBufferSamples     = 240;
constexpr float    kToneHz            = 440.0f;
constexpr float    kTwoPi             = 6.2831853071795864769f;
constexpr int32_t  kFullScale         = 32767;
constexpr int32_t  kToneAmplitude     = kFullScale / 20; // 10% full scale peak-to-peak.
constexpr int32_t  kNegativeDcOffset  = -kFullScale / 10;
constexpr int32_t  kPositiveDcOffset  = kFullScale / 10;
constexpr uint32_t kPumpYieldInterval = 16;

int16_t clamp_int16(int32_t sample)
{
    if (sample > INT16_MAX)
    {
        return INT16_MAX;
    }
    if (sample < INT16_MIN)
    {
        return INT16_MIN;
    }

    return static_cast<int16_t>(sample);
}

int32_t dc_offset_for_sample(uint32_t sample)
{
    return sample < kSegmentSamples ? kNegativeDcOffset : kPositiveDcOffset;
}

void synthesize_tone(int16_t* samples, size_t sampleCount, uint32_t startSample)
{
    for (size_t i = 0; i < sampleCount; ++i)
    {
        const uint32_t absolute_sample = startSample + i;
        const float phase = kTwoPi * kToneHz * static_cast<float>(absolute_sample) / static_cast<float>(kSampleRateHz);
        const int32_t sample = dc_offset_for_sample(absolute_sample) +
                               static_cast<int32_t>(sinf(phase) * static_cast<float>(kToneAmplitude));
        samples[i]           = clamp_int16(sample);
    }
}

bool queue_all(AudioFifo& fifo, const int16_t* samples, size_t sampleCount, const char* label)
{
    size_t queued = 0;
    while (queued < sampleCount)
    {
        const size_t written = fifo.queue(samples + queued, sampleCount - queued, kSampleRateHz);
        queued += written;
        if (queued >= sampleCount)
        {
            return true;
        }

        AudioFileRecorder::pump();
        taskYIELD();

        if (written == 0 && fifo.availableToWrite() == 0)
        {
            continue;
        }
        if (written == 0)
        {
            Serial.printf("%s: failed to queue %s samples\n", TAG, label);
            return false;
        }
    }

    return true;
}

} // namespace

void test_micfilterresult()
{
    Serial.begin(115200, SERIAL_8N1, -1, 1);
    delay(1000);

    Serial.println();
    Serial.printf("%s: starting mic filter result test\n", TAG);

    if (!AudioManager::init(AudioManager::Hardware::M5StackInternal))
    {
        Serial.printf("%s: AudioManager init failed\n", TAG);
        idle_forever();
    }

    Clock.syncToCompileTime();
    MicGainManager::init();
    MicGainManager::setBypass(false);
    AudioFileRecorder::setPurePcmMode(false);
    AudioFileRecorder::resetWriteDurationStats();

    if (!AudioFileRecorder::startRecording(AudioFileRecorder::RecordingType::Memo))
    {
        Serial.printf("%s: recording start failed\n", TAG);
        idle_forever();
    }

    Serial.printf("%s: recording path: %s\n", TAG, AudioFileRecorder::currentSdPath());
    Serial.printf("%s: tone_hz=%.1f segment_s=%lu total_s=%lu amplitude=%ld dc_offsets=%ld,%ld\n",
                  TAG,
                  static_cast<double>(kToneHz),
                  static_cast<unsigned long>(kSegmentSeconds),
                  static_cast<unsigned long>(kSegmentSeconds * 2U),
                  static_cast<long>(kToneAmplitude),
                  static_cast<long>(kNegativeDcOffset),
                  static_cast<long>(kPositiveDcOffset));

    int16_t raw_buffer[kBufferSamples];
    int16_t filtered_buffer[kBufferSamples];

    uint32_t submitted_samples = 0;
    while (submitted_samples < kTotalSamples)
    {
        const size_t chunk_samples = std::min<size_t>(kBufferSamples, kTotalSamples - submitted_samples);
        synthesize_tone(raw_buffer, chunk_samples, submitted_samples);
        memcpy(filtered_buffer, raw_buffer, chunk_samples * sizeof(filtered_buffer[0]));
        MicGainManager::process(filtered_buffer, chunk_samples);

        if (!queue_all(AudioManager::bluetoothToFileFifo(), raw_buffer, chunk_samples, "raw") ||
            !queue_all(AudioManager::micToFileFifo(), filtered_buffer, chunk_samples, "filtered"))
        {
            AudioFileRecorder::stopRecording(true);
            idle_forever();
        }

        AudioFileRecorder::pump();
        submitted_samples += chunk_samples;

        if (((submitted_samples / chunk_samples) % kPumpYieldInterval) == 0)
        {
            taskYIELD();
        }
    }

    Serial.printf("%s: submitted %lu samples per stream\n", TAG, static_cast<unsigned long>(submitted_samples));
    if (!AudioFileRecorder::stopRecording())
    {
        Serial.printf("%s: recording stop failed\n", TAG);
        idle_forever();
    }

    Serial.printf(
        "%s: recording complete: %s bytes=%llu raw_peak=%u scaled_peak=%u write_avg_ms=%.3f write_max_ms=%.3f\n",
        TAG,
        AudioFileRecorder::currentSdPath(),
        static_cast<unsigned long long>(AudioFileRecorder::bytesWritten()),
        static_cast<unsigned>(MicGainManager::rawPeakLevel()),
        static_cast<unsigned>(MicGainManager::scaledPeakLevel()),
        AudioFileRecorder::writeDurationAverageMs(),
        AudioFileRecorder::writeDurationMaxMs());

    idle_forever();
}
