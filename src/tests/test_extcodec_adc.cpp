#include <Arduino.h>

#include "AudioManager.h"
#include "ExtCodec.h"
#include "utilfuncs.h"

namespace
{

constexpr const char* TAG             = "test_extcodec_adc";
constexpr uint32_t    kReportPeriodMs = 200;
constexpr bool        kPrimeI2sOutputWithSilence = true;
constexpr size_t      kI2sSilenceSamples         = 240;
constexpr uint32_t    kI2sSilencePumpMs          = 40;

void print_sample()
{
    const uint32_t        now_ms = millis();
    const ExtCodec::State state  = ExtCodec::state();

    Serial.printf("%s: ms=%lu state=%s gen=%lu earbud_adc=%u inline_mic_adc=%u\n",
                  TAG,
                  static_cast<unsigned long>(now_ms),
                  ExtCodec::stateName(state),
                  static_cast<unsigned long>(ExtCodec::stateGeneration()),
                  static_cast<unsigned>(ExtCodec::earbudSenseRaw()),
                  static_cast<unsigned>(ExtCodec::inlineMicSenseRaw()));
}

void print_codec_diagnostics(const char* prefix)
{
    uint16_t   ana_status = 0;
    const bool ana_ok     = ExtCodec::readChipAnaStatus(ana_status);

    Serial.printf("%s: %s state=%s gen=%lu earbud_adc=%u inline_mic_adc=%u ana_ok=%u ana=0x%04X pll=%u\n",
                  TAG,
                  prefix ? prefix : "codec",
                  ExtCodec::stateName(ExtCodec::state()),
                  static_cast<unsigned long>(ExtCodec::stateGeneration()),
                  static_cast<unsigned>(ExtCodec::earbudSenseRaw()),
                  static_cast<unsigned>(ExtCodec::inlineMicSenseRaw()),
                  ana_ok ? 1U : 0U,
                  static_cast<unsigned>(ana_status),
                  ExtCodec::pllLocked() ? 1U : 0U);
}

void halt_if_codec_init_failed(bool i2s_ok, bool extcodec_ok, bool initialized, bool available)
{
    if (i2s_ok && extcodec_ok && initialized && available)
    {
        return;
    }

    Serial.printf("%s: HALT codec init failed i2s=%u extcodec=%u initialized=%u available=%u\n",
                  TAG,
                  i2s_ok ? 1U : 0U,
                  extcodec_ok ? 1U : 0U,
                  initialized ? 1U : 0U,
                  available ? 1U : 0U);
    print_codec_diagnostics("failed-init");
    Serial.flush();
    idle_forever();
}

void prime_i2s_output_with_silence()
{
    if (!kPrimeI2sOutputWithSilence)
    {
        return;
    }

    const bool audio_ok = AudioManager::init(AudioManager::Hardware::ExternalI2SCodec);
    const bool i2s_ok   = audio_ok && AudioManager::enableFullDuplexMode(AudioManager::kSampleRateHz);
    if (!i2s_ok)
    {
        Serial.printf("%s: silence prime skipped audio=%u i2s=%u\n", TAG, audio_ok ? 1U : 0U, i2s_ok ? 1U : 0U);
        return;
    }

    AudioFifo& fifo = AudioManager::bluetoothToSpeakerFifo();
    fifo.clear();
    const size_t queued = fifo.queueSilence(kI2sSilenceSamples);
    const uint32_t pump_until_ms = millis() + kI2sSilencePumpMs;
    while (fifo.availableToRead() > 0 || static_cast<int32_t>(pump_until_ms - millis()) > 0)
    {
        AudioManager::pump_bt2spk();
        taskYIELD();
    }

    Serial.printf("%s: silence prime queued=%u samples\n", TAG, static_cast<unsigned>(queued));
}

} // namespace

void test_extcodec_adc()
{
    Serial.begin(115200, SERIAL_8N1, -1, 1);
    delay(250);

    Serial.println();
    Serial.printf("%s: starting ADC state monitor period=%lu ms\n",
                  TAG,
                  static_cast<unsigned long>(kReportPeriodMs));

    const bool i2s_ok       = AudioManager::configure_i2s_shared();
    const bool extcodec_ok  = ExtCodec::init();
    const bool initialized  = ExtCodec::initialized();
    const bool available    = ExtCodec::available();

    Serial.printf("%s: init i2s=%u extcodec=%u initialized=%u available=%u\n",
                  TAG,
                  i2s_ok ? 1U : 0U,
                  extcodec_ok ? 1U : 0U,
                  initialized ? 1U : 0U,
                  available ? 1U : 0U);
    print_codec_diagnostics("post-init");
    halt_if_codec_init_failed(i2s_ok, extcodec_ok, initialized, available);

    prime_i2s_output_with_silence();

    while (true)
    {
        print_sample();
        delay(kReportPeriodMs);
    }
}
