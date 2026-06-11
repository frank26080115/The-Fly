#include <Arduino.h>

#include "AudioManager.h"
#include "ExtCodec.h"
#include "utilfuncs.h"

namespace
{

constexpr const char* TAG                 = "test_continuous_sine";
constexpr uint32_t    kSampleRateHz        = AudioManager::kSampleRateHz;
constexpr uint32_t    kSineFrequencyHz     = 440;
constexpr size_t      kSineTableSize       = 64;
constexpr size_t      kChunkSamples        = 240;
constexpr uint32_t    kPhaseScale          = 65536;
constexpr uint32_t    kStatusReportPeriodMs = 1000;
constexpr uint32_t    kPhaseStep =
    static_cast<uint32_t>((static_cast<uint64_t>(kSineFrequencyHz) * kSineTableSize * kPhaseScale +
                           (kSampleRateHz / 2U)) /
                          kSampleRateHz);

constexpr int16_t kSineTable[kSineTableSize] = {
    0,    803,  1598,  2378,  3135,  3861,  4551,  5196,
    5792, 6332, 6811,  7224,  7567,  7838,  8034,  8152,
    8191, 8152, 8034,  7838,  7567,  7224,  6811,  6332,
    5792, 5196, 4551,  3861,  3135,  2378,  1598,  803,
    0,    -803, -1598, -2378, -3135, -3861, -4551, -5196,
    -5792, -6332, -6811, -7224, -7567, -7838, -8034, -8152,
    -8191, -8152, -8034, -7838, -7567, -7224, -6811, -6332,
    -5792, -5196, -4551, -3861, -3135, -2378, -1598, -803,
};

int16_t sine_sample(uint32_t& phase)
{
    const size_t index = (phase >> 16U) & (kSineTableSize - 1U);
    phase += kPhaseStep;
    return kSineTable[index];
}

void fill_sine_chunk(int16_t* samples, size_t sample_count, uint32_t& phase)
{
    for (size_t i = 0; i < sample_count; ++i)
    {
        samples[i] = sine_sample(phase);
    }
}

void pump_until_writable(AudioFifo& fifo)
{
    while (fifo.availableToWrite() == 0)
    {
        AudioManager::pump_bt2spk();
        taskYIELD();
    }
}

void queue_sine_chunk(AudioFifo& fifo, uint32_t& phase, uint64_t& generated_samples)
{
    int16_t samples[kChunkSamples];

    pump_until_writable(fifo);

    const size_t writable = fifo.availableToWrite();
    const size_t chunk    = min(kChunkSamples, writable);
    fill_sine_chunk(samples, chunk, phase);

    size_t queued = 0;
    while (queued < chunk)
    {
        const size_t written = fifo.queue(samples + queued, chunk - queued, kSampleRateHz);
        queued += written;
        AudioManager::pump_bt2spk();
        if (written == 0)
        {
            taskYIELD();
        }
    }

    generated_samples += queued;
}

void report_status(uint64_t generated_samples)
{
    const AudioFifo::FlowEvents flow = AudioFifo::takeGlobalFlowEvents();
    const AudioFifo&            fifo = AudioManager::bluetoothToSpeakerFifo();

    Serial.printf("%s: t=%lus generated=%llu fifo=%u%% used=%u flow_over=%lu flow_under=%lu\n",
                  TAG,
                  static_cast<unsigned long>(millis() / 1000U),
                  static_cast<unsigned long long>(generated_samples),
                  static_cast<unsigned>(fifo.getFillPercentage()),
                  static_cast<unsigned>(fifo.usedSamples()),
                  static_cast<unsigned long>(flow.overflow),
                  static_cast<unsigned long>(flow.underflow));
}

} // namespace

void test_continuous_sine()
{
    Serial.begin(115200, SERIAL_8N1, -1, 1);
    delay(250);

    Serial.println();
    Serial.printf("%s: starting %lu Hz continuous sine test at %lu Hz sample rate\n",
                  TAG,
                  static_cast<unsigned long>(kSineFrequencyHz),
                  static_cast<unsigned long>(kSampleRateHz));
    Serial.printf("%s: PCM amplitude is +/-8191, about 1/4 full-scale int16\n", TAG);
    Serial.flush();

    const bool i2s_ok = AudioManager::configure_i2s_shared();
    const bool codec_ok = ExtCodec::init();
    const bool audio_ok = AudioManager::init(AudioManager::Hardware::ExternalI2SCodec);
    const bool speaker_ok = audio_ok && AudioManager::enableSpeakerMode(kSampleRateHz);

    if (!i2s_ok || !codec_ok || !audio_ok || !speaker_ok)
    {
        Serial.printf("%s: init failed i2s=%u codec=%u audio=%u speaker=%u\n",
                      TAG,
                      i2s_ok ? 1U : 0U,
                      codec_ok ? 1U : 0U,
                      audio_ok ? 1U : 0U,
                      speaker_ok ? 1U : 0U);
        Serial.flush();
        idle_forever();
    }

    AudioManager::setSpeakerMuted(false);
    AudioManager::setVolume(AudioManager::kMaxVolume);
    AudioManager::bluetoothToSpeakerFifo().clear();
    AudioFifo::takeGlobalFlowEvents();

    Serial.printf("%s: external codec speaker path enabled; NS4168 should remain off\n", TAG);
    Serial.flush();

    uint32_t phase               = 0;
    uint64_t generated_samples   = 0;
    uint32_t next_report_ms      = millis() + kStatusReportPeriodMs;
    AudioFifo& speaker_fifo      = AudioManager::bluetoothToSpeakerFifo();

    while (true)
    {
        queue_sine_chunk(speaker_fifo, phase, generated_samples);
        AudioManager::pump_bt2spk();

        const uint32_t now_ms = millis();
        if (static_cast<int32_t>(now_ms - next_report_ms) >= 0)
        {
            report_status(generated_samples);
            next_report_ms = now_ms + kStatusReportPeriodMs;
        }

        taskYIELD();
    }
}
