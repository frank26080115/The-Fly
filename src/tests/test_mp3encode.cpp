#include <Arduino.h>
#include <SdFat.h>
#include <math.h>
#include <new>
#include <string.h>

#include "AudioFileRecorder.h"
#include "AudioManager.h"
#include "MicroSdCard.h"
#include "conf.h"
#include "utilfuncs.h"

#define TEST_MP3ENCODE_USE_RANDOM_INPUT 1

#ifndef TEST_MP3ENCODE_USE_RANDOM_INPUT
#define TEST_MP3ENCODE_USE_RANDOM_INPUT 0
#endif

namespace
{

constexpr const char* TAG                   = "test_mp3encode";
constexpr uint32_t    kSampleRateHz         = AUDIO_RECORDER_SAMPLE_RATE_HZ;
constexpr uint32_t    kDurationMs           = 10000;
constexpr uint32_t    kComfortTargetAudioMs = kDurationMs * 2;
constexpr size_t      kChunkFrames          = MP3_PCM_FRAMES_PER_MP3_FRAME;
constexpr size_t      kSineTableBits        = 10;
constexpr size_t      kSineTableSize        = 1U << kSineTableBits;
constexpr float       kLeftHz               = 440.0f;
constexpr float       kRightHz              = 660.0f;
constexpr float       kAmplitude            = 14000.0f;
constexpr float       kTwoPi                = 6.28318530717958647692f;
constexpr uint32_t    kRandomSeed           = 0xA53C9E2DU;

int16_t  g_left_samples[kChunkFrames];
int16_t  g_right_samples[kChunkFrames];
int16_t  g_sine_table[kSineTableSize];
uint32_t g_left_phase       = 0;
uint32_t g_right_phase      = 0;
uint32_t g_left_phase_step  = 0;
uint32_t g_right_phase_step = 0;
uint32_t g_random_state     = kRandomSeed;

bool has_mp3_extension(const char* path)
{
    if (!path)
    {
        return false;
    }

    const size_t len = strlen(path);
    return len >= 4 && path[len - 4] == '.' && (path[len - 3] == 'm' || path[len - 3] == 'M') &&
           (path[len - 2] == 'p' || path[len - 2] == 'P') && path[len - 1] == '3';
}

uint32_t sine_phase_step(float frequency_hz)
{
    const double cycles_per_sample = static_cast<double>(frequency_hz) / static_cast<double>(kSampleRateHz);
    return static_cast<uint32_t>((cycles_per_sample * 4294967296.0) + 0.5);
}

void init_sine_generator()
{
    for (size_t i = 0; i < kSineTableSize; ++i)
    {
        const float phase = (kTwoPi * static_cast<float>(i)) / static_cast<float>(kSineTableSize);
        g_sine_table[i]   = static_cast<int16_t>(sinf(phase) * kAmplitude);
    }

    g_left_phase       = 0;
    g_right_phase      = 0;
    g_left_phase_step  = sine_phase_step(kLeftHz);
    g_right_phase_step = sine_phase_step(kRightHz);
}

void generate_sine_chunk(size_t frames)
{
    for (size_t i = 0; i < frames; ++i)
    {
        g_left_samples[i]  = g_sine_table[(g_left_phase >> (32 - kSineTableBits)) & (kSineTableSize - 1)];
        g_right_samples[i] = g_sine_table[(g_right_phase >> (32 - kSineTableBits)) & (kSineTableSize - 1)];
        g_left_phase += g_left_phase_step;
        g_right_phase += g_right_phase_step;
    }
}

uint32_t next_random_u32()
{
    uint32_t value = g_random_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    g_random_state = value;
    return value;
}

void init_sample_generator()
{
#if TEST_MP3ENCODE_USE_RANDOM_INPUT
    g_random_state = kRandomSeed;
#else
    init_sine_generator();
#endif
}

void generate_sample_chunk(size_t frames)
{
#if TEST_MP3ENCODE_USE_RANDOM_INPUT
    for (size_t i = 0; i < frames; ++i)
    {
        const uint32_t left  = next_random_u32();
        const uint32_t right = next_random_u32();
        g_left_samples[i]   = static_cast<int16_t>(left & 0xFFFFU);
        g_right_samples[i]  = static_cast<int16_t>(right & 0xFFFFU);
    }
#else
    generate_sine_chunk(frames);
#endif
}

const char* sample_generator_name()
{
#if TEST_MP3ENCODE_USE_RANDOM_INPUT
    return "independent int16 pseudo-random noise";
#else
    return "two sine waves";
#endif
}

const char* sample_generator_init_step_name()
{
#if TEST_MP3ENCODE_USE_RANDOM_INPUT
    return "random generator init";
#else
    return "sine generator init";
#endif
}

uint64_t queue_stereo_chunk_until(
    AudioFifo& left_fifo, AudioFifo& right_fifo, uint32_t started_ms, uint32_t duration_ms, bool& ok)
{
    uint64_t total_queued     = 0;
    size_t   queued           = 0;
    uint32_t last_progress_ms = millis();
    ok                        = true;

    generate_sample_chunk(kChunkFrames);

    while (static_cast<uint32_t>(millis() - started_ms) < duration_ms)
    {
        const size_t left_space  = left_fifo.availableToWrite();
        const size_t right_space = right_fifo.availableToWrite();
        size_t       to_queue    = kChunkFrames - queued;
        to_queue                 = min(to_queue, left_space);
        to_queue                 = min(to_queue, right_space);

        if (to_queue > 0)
        {
            const size_t left_written  = left_fifo.queue(g_left_samples + queued, to_queue, kSampleRateHz);
            const size_t right_written = right_fifo.queue(g_right_samples + queued, to_queue, kSampleRateHz);
            if (left_written != to_queue || right_written != to_queue)
            {
                Serial.printf("%s: FIFO queue mismatch left=%u right=%u requested=%u\n",
                              TAG,
                              static_cast<unsigned>(left_written),
                              static_cast<unsigned>(right_written),
                              static_cast<unsigned>(to_queue));
                ok = false;
                return total_queued;
            }

            queued += to_queue;
            total_queued += to_queue;
            last_progress_ms = millis();

            if (queued == kChunkFrames)
            {
                queued = 0;
                generate_sample_chunk(kChunkFrames);
            }
        }

        AudioFileRecorder::pump();
        if (to_queue == 0)
        {
            taskYIELD();
            if (millis() - last_progress_ms > 5000)
            {
                Serial.printf("%s: timed out waiting for recorder FIFO space\n", TAG);
                ok = false;
                return total_queued;
            }
        }
    }

    return total_queued;
}

uint32_t elapsed_ms_since(uint32_t started_ms)
{
    return static_cast<uint32_t>(millis() - started_ms);
}

void print_step_elapsed(const char* step, uint32_t started_ms)
{
    Serial.printf("%s: %s elapsed=%lu ms\n", TAG, step, static_cast<unsigned long>(elapsed_ms_since(started_ms)));
}

uint64_t read_file_size(const char* path)
{
    FsFile file;
    if (!file.open(path, O_RDONLY))
    {
        return 0;
    }

    const uint64_t size = file.fileSize();
    file.close();
    return size;
}

uint64_t print_duration_estimate(uint64_t file_size)
{
    const uint64_t byte_duration_ms =
        (file_size * 1000ULL + (MP3_CBR_BYTES_PER_SECOND / 2ULL)) / MP3_CBR_BYTES_PER_SECOND;
    const uint64_t whole_mp3_frames = file_size / MP3_CBR_BYTES_PER_MP3_FRAME;
    const uint64_t frame_duration_ms =
        (whole_mp3_frames * MP3_PCM_FRAMES_PER_MP3_FRAME * 1000ULL + (kSampleRateHz / 2ULL)) / kSampleRateHz;

    Serial.printf("%s: final file size=%llu bytes\n", TAG, static_cast<unsigned long long>(file_size));
    Serial.printf("%s: CBR bitrate=%u kbps, bytes_per_second=%u\n",
                  TAG,
                  static_cast<unsigned>(MP3_BITRATE_KBPS),
                  static_cast<unsigned>(MP3_CBR_BYTES_PER_SECOND));
    Serial.printf("%s: byte-derived duration=%llu ms (%.3f seconds)\n",
                  TAG,
                  static_cast<unsigned long long>(byte_duration_ms),
                  static_cast<double>(byte_duration_ms) / 1000.0);
    Serial.printf("%s: whole-frame-derived duration=%llu ms from %llu whole MP3 frames, remainder=%llu bytes\n",
                  TAG,
                  static_cast<unsigned long long>(frame_duration_ms),
                  static_cast<unsigned long long>(whole_mp3_frames),
                  static_cast<unsigned long long>(file_size % MP3_CBR_BYTES_PER_MP3_FRAME));
    return byte_duration_ms;
}

} // namespace

void test_mp3encode()
{
    Serial.begin(115200);
    delay(250);

    Serial.println();
    Serial.println("starting MP3 encoder recording test");
    Serial.printf(
        "%s: throughput test wall=%u ms comfort_target_audio=%u ms input=%s sample_rate=%u Hz\n",
        TAG,
        static_cast<unsigned>(kDurationMs),
        static_cast<unsigned>(kComfortTargetAudioMs),
        sample_generator_name(),
        static_cast<unsigned>(kSampleRateHz));
#if !TEST_MP3ENCODE_USE_RANDOM_INPUT
    Serial.printf("%s: sine input left=%.1f Hz right=%.1f Hz amplitude=%.1f\n",
                  TAG,
                  static_cast<double>(kLeftHz),
                  static_cast<double>(kRightHz),
                  static_cast<double>(kAmplitude));
#endif

    uint32_t step_started_ms = millis();
    init_sample_generator();
    print_step_elapsed(sample_generator_init_step_name(), step_started_ms);

#if !defined(BUILD_USE_MP3_COMPRESSION)
    Serial.printf("%s: BUILD_USE_MP3_COMPRESSION is not enabled; this test expects MP3 recording\n", TAG);
    idle_forever();
#endif

#if BUILD_WITH_SECURITY_LEVEL != 0
    Serial.printf(
        "%s: BUILD_WITH_SECURITY_LEVEL=%d; this test is intended for unencrypted .mp3 output at security level 0\n",
        TAG,
        BUILD_WITH_SECURITY_LEVEL);
#endif

    step_started_ms = millis();
    if (!AudioManager::init())
    {
        Serial.printf("%s: AudioManager init failed\n", TAG);
        idle_forever();
    }
    print_step_elapsed("AudioManager init", step_started_ms);

    step_started_ms = millis();
    if (!MicroSdCard::begin())
    {
        Serial.printf("%s: microSD init failed\n", TAG);
        idle_forever();
    }
    print_step_elapsed("microSD init", step_started_ms);

    step_started_ms = millis();
    if (!AudioFileRecorder::startRecording('P'))
    {
        Serial.printf("%s: recording start failed\n", TAG);
        idle_forever();
    }
    print_step_elapsed("AudioFileRecorder start", step_started_ms);

    char recording_path[64] = {};
    strlcpy(recording_path, AudioFileRecorder::currentSdPath(), sizeof(recording_path));
    Serial.printf("%s[%lu]: recording path=%s\n", TAG, millis(), recording_path);
    if (!has_mp3_extension(recording_path))
    {
        Serial.printf("%s: warning: expected .mp3 extension under this configuration\n", TAG);
    }

    AudioFifo& right_fifo = AudioManager::bluetoothToFileFifo();
    AudioFifo& left_fifo  = AudioManager::micToFileFifo();

    const uint64_t realtime_frames      = (static_cast<uint64_t>(kSampleRateHz) * kDurationMs) / 1000ULL;
    const uint32_t recording_started_ms = millis();
    bool           queue_ok             = true;
    const uint64_t submitted_frames =
        queue_stereo_chunk_until(left_fifo, right_fifo, recording_started_ms, kDurationMs, queue_ok);
    if (!queue_ok)
    {
        Serial.printf("%s[%lu]: sample queue failed after %llu frames\n",
                      TAG,
                      millis(),
                      static_cast<unsigned long long>(submitted_frames));
        AudioFileRecorder::stopRecording(true);
        idle_forever();
    }

    const uint32_t submit_elapsed_ms  = elapsed_ms_since(recording_started_ms);
    const uint64_t submitted_audio_ms = (submitted_frames * 1000ULL + (kSampleRateHz / 2ULL)) / kSampleRateHz;
    const double   submitted_realtime_ratio =
        submit_elapsed_ms > 0 ? static_cast<double>(submitted_audio_ms) / static_cast<double>(submit_elapsed_ms) : 0.0;
    Serial.printf("%s[%lu]: submitted %llu stereo frames over %lu ms, realtime_reference=%llu frames\n",
                  TAG,
                  millis(),
                  static_cast<unsigned long long>(submitted_frames),
                  static_cast<unsigned long>(submit_elapsed_ms),
                  static_cast<unsigned long long>(realtime_frames));
    Serial.printf("%s: submitted-audio duration=%llu ms (%.3f seconds), throughput=%.2fx realtime\n",
                  TAG,
                  static_cast<unsigned long long>(submitted_audio_ms),
                  static_cast<double>(submitted_audio_ms) / 1000.0,
                  submitted_realtime_ratio);

    const uint32_t stop_started_ms = millis();
    if (!AudioFileRecorder::stopRecording())
    {
        Serial.printf("%s: recording stop failed\n", TAG);
        idle_forever();
    }
    Serial.printf("%s: stop/flush elapsed=%lu ms, total active+close elapsed=%lu ms\n",
                  TAG,
                  static_cast<unsigned long>(elapsed_ms_since(stop_started_ms)),
                  static_cast<unsigned long>(elapsed_ms_since(recording_started_ms)));
    Serial.printf("%s: write avg=%.3f ms max=%.3f ms long_write=%u latched=%u\n",
                  TAG,
                  AudioFileRecorder::writeDurationAverageMs(),
                  AudioFileRecorder::writeDurationMaxMs(),
                  AudioFileRecorder::longWrite() ? 1U : 0U,
                  AudioFileRecorder::longWriteSinceReset() ? 1U : 0U);

    uint64_t final_size = read_file_size(recording_path);
    if (final_size == 0)
    {
        final_size = AudioFileRecorder::bytesWritten();
        Serial.printf("%s: reopened file size unavailable, using recorder bytesWritten\n", TAG);
    }

    const uint64_t final_file_duration_ms = print_duration_estimate(final_size);
    const double   final_file_realtime_ratio =
        submit_elapsed_ms > 0 ? static_cast<double>(final_file_duration_ms) / static_cast<double>(submit_elapsed_ms)
                              : 0.0;
    Serial.printf(
        "%s: comfort target result=%s final_file_audio=%llu ms target_audio=%u ms, file throughput=%.2fx realtime\n",
        TAG,
        final_file_duration_ms >= kComfortTargetAudioMs ? "PASS" : "FAIL",
        static_cast<unsigned long long>(final_file_duration_ms),
        static_cast<unsigned>(kComfortTargetAudioMs),
        final_file_realtime_ratio);
    Serial.println("MP3 encoder recording test finished");

    idle_forever();
}
