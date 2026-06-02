#include <Arduino.h>

#include "AudioManager.h"
#include "utilfuncs.h"

namespace
{

constexpr const char* TAG            = "test_speaker";
constexpr uint32_t    kSampleRateHz  = AudioManager::kSampleRateHz;
constexpr size_t      kToneSamples   = kSampleRateHz / 2;
constexpr size_t      kChunkSamples  = 240;
constexpr int16_t     kToneAmplitude = 10000;
constexpr int         kI2sPort       = 0;
constexpr int         kI2sBclkPin    = 12;
constexpr int         kI2sLrckPin    = 0;
constexpr int         kI2sDoutPin    = 2;

int16_t square_sample(size_t sampleIndex, uint32_t frequencyHz)
{
    const uint32_t half_cycle = (static_cast<uint32_t>(sampleIndex) * frequencyHz * 2U) / kSampleRateHz;
    return (half_cycle & 1U) ? -kToneAmplitude : kToneAmplitude;
}

void pump_until_writable(AudioFifo& fifo)
{
    while (fifo.availableToWrite() == 0)
    {
        AudioManager::pump_bt2spk();
        taskYIELD();
    }
}

void queue_square_tone(uint32_t frequencyHz)
{
    AudioFifo& fifo = AudioManager::bluetoothToSpeakerFifo();
    int16_t    samples[kChunkSamples];
    size_t     generated = 0;

    Serial.printf("%s: queueing %lu Hz square wave for 500 ms\n", TAG, static_cast<unsigned long>(frequencyHz));
    while (generated < kToneSamples)
    {
        pump_until_writable(fifo);

        const size_t chunk = min(kChunkSamples, kToneSamples - generated);
        for (size_t i = 0; i < chunk; ++i)
        {
            samples[i] = square_sample(generated + i, frequencyHz);
        }

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

        generated += chunk;
    }
}

void drain_speaker()
{
    AudioFifo& fifo = AudioManager::bluetoothToSpeakerFifo();

    Serial.printf("%s: pumping FIFO to I2S speaker output\n", TAG);
    const uint32_t settle_until_ms = millis() + 1500;
    while (fifo.availableToRead() > 0 || static_cast<int32_t>(settle_until_ms - millis()) > 0)
    {
        AudioManager::pump_bt2spk();
        taskYIELD();
    }
}

} // namespace

void test_speaker()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.printf("%s: starting speaker smoke test\n", TAG);
    Serial.printf("%s: initializing AudioManager\n", TAG);
    if (!AudioManager::init(AudioManager::Hardware::M5StackInternal))
    {
        Serial.printf("%s: AudioManager init failed\n", TAG);
        idle_forever();
    }

    Serial.printf("%s: enabling built-in speaker\n", TAG);
    Serial.printf("%s: enabling I2S TX: port=%d rate=%lu Hz BCLK=%d LRCK=%d DOUT=%d mono slot=BOTH\n",
                  TAG,
                  kI2sPort,
                  static_cast<unsigned long>(kSampleRateHz),
                  kI2sBclkPin,
                  kI2sLrckPin,
                  kI2sDoutPin);
    if (!AudioManager::enableSpeakerMode())
    {
        Serial.printf("%s: built-in speaker init failed\n", TAG);
        idle_forever();
    }
    Serial.printf("%s: I2S speaker mode enabled\n", TAG);

    AudioManager::setVolume(AudioManager::kMaxVolume);
    AudioManager::bluetoothToSpeakerFifo().clear();

    queue_square_tone(440);
    queue_square_tone(880);
    drain_speaker();

    Serial.printf("%s: speaker smoke test complete\n", TAG);
    idle_forever();
}
