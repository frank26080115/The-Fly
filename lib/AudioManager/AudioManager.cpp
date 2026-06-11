// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "AudioManager.h"

#include <M5Unified.h>
#include <Wire.h>
#include <algorithm>
#include <mutex>
#include <string.h>

#include "AudioFileRecorder.h"
#include "MicGainManager.h"
#include "SpeakerPeakActivity.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_rom_gpio.h"
#include "hal/gpio_hal.h"
#include "dbg_log.h"
#include "BluetoothManager.h"
#include "ExtCodec.h"
#include "diagnostics.h"
#include "pins.h"
#include "soc/gpio_sig_map.h"
#include "soc/io_mux_reg.h"
#include "control_sgtl5000.h"
#include "utilfuncs.h"

namespace AudioManager
{
namespace
{

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

constexpr const char* TAG = "AudioManager";

constexpr i2s_port_t kI2sPort = I2S_NUM_0;

constexpr size_t   kPumpSamples                   = 240;
constexpr size_t   kDmaBufferCount                = 8;
constexpr uint8_t  kVolumeStep                    = 3;
constexpr uint8_t  kVolumeGainShift               = 10;
constexpr size_t   kHfpOutgoingNotifyMinSamples   = AUDIOFIFO_MS_TO_SAMPLES_16K(50);
constexpr uint16_t kVolumeGainByLevel[kMaxVolume] = {
    // gain = 10^(dB / 20) ; -50 dB was used to generate this table
    3,  4,  5,  6,   7,   9,   11,  13,  16,  19,  24,  29,  35,  43,  52,
    64, 78, 95, 115, 141, 172, 209, 255, 311, 380, 463, 565, 688, 840, 1024,
};

constexpr size_t kFileFifoCatchupStartSamples  = 1024;
constexpr size_t kFileFifoCatchupTargetSamples = 512;
constexpr size_t kI2sTxCreditLimitBytes        = kPumpSamples * kDmaBufferCount * 2 * sizeof(int16_t);

// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------

enum class SpeakerPath
{
    None,
    NS4168,
    ExternalI2SCodec,
};

enum class I2sConfig
{
    None,
    SharedStd,
    InternalPdm,
};

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

AudioFifo g_fifo_bt2spk  (AUDIOFIFO_MS_TO_SAMPLES_16K(500), AUDIOFIFO_MS_TO_SAMPLES_16K(100));
AudioFifo g_fifo_mic2bt  (AUDIOFIFO_MS_TO_SAMPLES_16K(500), AUDIOFIFO_MS_TO_SAMPLES_16K(100));
AudioFifo g_fifo_bt2file (AUDIOFIFO_MS_TO_SAMPLES_16K(500), AUDIOFIFO_MS_TO_SAMPLES_16K(50));
AudioFifo g_fifo_mic2file(AUDIOFIFO_MS_TO_SAMPLES_16K(500), AUDIOFIFO_MS_TO_SAMPLES_16K(50));

Hardware          g_hardware               = Hardware::M5StackInternal;
P2TMode           g_mode                   = P2TMode::Stopped;
SpeakerPath       g_speaker_path           = SpeakerPath::None;
i2s_chan_handle_t g_i2s_tx                 = nullptr;
i2s_chan_handle_t g_i2s_rx                 = nullptr;
I2sConfig         g_i2s_config             = I2sConfig::None;
uint32_t          g_i2s_sample_rate_hz     = 0;
uint8_t           g_volume                 = kMaxVolume;
bool              g_speaker_muted          = false;
uint8_t           g_speaker_restore_volume = kMaxVolume;
bool              g_initialized            = false;
bool              g_wire_started           = false;
HfpCodec          g_hfp_codec              = HfpCodec::Msbc;
uint32_t          g_hfp_rate_hz            = kSampleRateHz;
volatile size_t   g_i2s_tx_available_bytes = 0;
portMUX_TYPE      g_i2s_tx_credit_mux      = portMUX_INITIALIZER_UNLOCKED;
std::mutex        g_pump_mutex;
size_t            g_pending_speaker_bytes = 0;

int16_t g_mono_buffer[kPumpSamples];
int16_t g_stereo_buffer[kPumpSamples * 2];
uint8_t g_pending_speaker_buffer[kPumpSamples * 2 * sizeof(int16_t)];

#ifdef ENABLE_HFP_AUDIO_DIAGNOSTICS
HfpAudioDiagnostics g_hfp_diag     = {};
portMUX_TYPE        g_hfp_diag_mux = portMUX_INITIALIZER_UNLOCKED;
#endif

// -----------------------------------------------------------------------------
// Diagnostics Support
// -----------------------------------------------------------------------------

#ifdef ENABLE_HFP_AUDIO_DIAGNOSTICS
template <typename Updater> void update_hfp_diag(Updater updater)
{
    portENTER_CRITICAL(&g_hfp_diag_mux);
    updater(g_hfp_diag);
    portEXIT_CRITICAL(&g_hfp_diag_mux);
}

#define HFP_AUDIO_DIAG(...) update_hfp_diag(__VA_ARGS__)
#else
#define HFP_AUDIO_DIAG(...)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
    } while (false)
#endif

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

bool   begin_fifos();
void   stop_audio_mode();
void   stop_i2s();
bool   configure_i2s_shared_impl(uint32_t sampleRateHz = kSampleRateHz);
bool   enable_ns4168_speaker(uint32_t sampleRateHz = kSampleRateHz);
bool   enable_spm1423_mic();
bool   enable_exti2scodec(P2TMode nextMode, uint32_t sampleRateHz = kSampleRateHz);
bool   ensure_exti2scodec_i2s(uint32_t sampleRateHz);
bool   sync_external_codec_routing();
void   duplicate_i2s0_bclk_to_gpio13();
void   queue_hfp_pcm(const uint8_t* buf, uint32_t len);
bool   should_notify_hfp_outgoing_ready(size_t queued_bt, bool allow_existing_fifo);
void   notify_hfp_outgoing_ready(bool ready);
bool   mic_file_source_active();
void   queue_silence_to_match(AudioFifo& lagging_fifo, AudioFifo& leading_fifo);
void   queue_silence_to_reduce_lag(AudioFifo& lagging_fifo, AudioFifo& leading_fifo);
void   set_ns4168_speaker_enabled(bool enabled);
void   set_external_codec_headphone_enabled(bool enabled);
bool   using_ns4168_speaker();
bool   speaker_output_stereo();
bool   speaker_output_active();
bool   mic_input_active();
uint32_t speaker_sample_rate_or_default(uint32_t sampleRateHz);
size_t speaker_bytes_per_frame();
void   apply_speaker_software_volume(int16_t* samples, size_t sampleCount);
size_t take_i2s_tx_frames(size_t maxFrames);
void   return_i2s_tx_bytes(size_t bytes);
bool   i2s_transfer_ready(i2s_chan_handle_t, i2s_event_data_t* event, void*);
bool   i2s_rx_transfer_ready(i2s_chan_handle_t, i2s_event_data_t*, void*);
bool   register_i2s_callbacks(i2s_chan_handle_t handle, bool rx);
bool   preload_silence(i2s_chan_handle_t handle);
bool   enable_channel(i2s_chan_handle_t handle, const char* label);
void   add_i2s_tx_credit(size_t bytes);
void   clear_i2s_tx_credit();
void   log_heap_after_fifo_begin(const char* fifo_name, bool result);

void note_speaker_i2s_write(size_t requested, size_t written, size_t bytes_per_frame, esp_err_t err);

} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

bool init(Hardware hardware)
{
    g_hardware = hardware;

    if (!begin_fifos())
    {
        DBG_LOGE(TAG, "AudioManager begin_fifos failed");
        return false;
    }

    g_fifo_bt2spk.clear();
    g_fifo_bt2file.clear();
    g_fifo_mic2bt.clear();
    g_fifo_mic2file.clear();
    g_fifo_mic2bt.setMuted(false);
    g_fifo_mic2file.setMuted(false);
    MicGainManager::init();
    SpeakerPeakActivity::init();

    if (!AudioFileRecorder::init(g_fifo_bt2file, g_fifo_mic2file))
    {
        DBG_LOGE(TAG, "AudioFileRecorder initialization failed");
        return false;
    }

    g_initialized = true;
    return true;
}

void stop()
{
    if (g_i2s_config == I2sConfig::InternalPdm)
    {
        stop_i2s();
        configure_i2s_shared_impl(kSampleRateHz);
        return;
    }

    stop_audio_mode();
}

bool enableSpeakerMode(uint32_t sampleRateHz)
{
    if (!g_initialized)
    {
        init(g_hardware);
    }

    sampleRateHz = speaker_sample_rate_or_default(sampleRateHz);
    return g_hardware == Hardware::M5StackInternal ? enable_ns4168_speaker(sampleRateHz)
                                                   : enable_exti2scodec(P2TMode::Speaker, sampleRateHz);
}

bool enableMicMode()
{
    if (!g_initialized)
    {
        init(g_hardware);
    }

    return g_hardware == Hardware::M5StackInternal ? enable_spm1423_mic() : enable_exti2scodec(P2TMode::Mic);
}

bool enableFullDuplexMode(uint32_t sampleRateHz)
{
    if (!g_initialized)
    {
        init(g_hardware);
    }

    sampleRateHz = speaker_sample_rate_or_default(sampleRateHz);
    if (g_hardware != Hardware::ExternalI2SCodec)
    {
        DBG_LOGE(TAG, "Full duplex audio requires the external I2S codec");
        return false;
    }

    return enable_exti2scodec(P2TMode::FullDuplex, sampleRateHz);
}

bool syncExternalCodecRouting()
{
    if (g_hardware != Hardware::ExternalI2SCodec || !ExtCodec::available())
    {
        return true;
    }

    const bool routed = sync_external_codec_routing();
    return routed;
}

P2TMode mode()
{
    return g_mode;
}

// -----------------------------------------------------------------------------
// Audio Pump
// -----------------------------------------------------------------------------

void pump_bt2spk()
{
    SpeakerPeakActivity::decay_peak();

    std::unique_lock<std::mutex> lock(g_pump_mutex, std::try_to_lock);
    if (!lock.owns_lock())
    {
        return;
    }

    if (!speaker_output_active() || !g_i2s_tx)
    {
        return;
    }

    if (g_pending_speaker_bytes > 0)
    {
        const size_t bytes_per_frame = speaker_bytes_per_frame();
        const size_t frames_to_write = take_i2s_tx_frames(g_pending_speaker_bytes / bytes_per_frame);
        const size_t bytes_to_write  = frames_to_write * bytes_per_frame;
        if (bytes_to_write == 0)
        {
            return;
        }

        size_t          written = 0;
        const esp_err_t err     = i2s_channel_write(g_i2s_tx, g_pending_speaker_buffer, bytes_to_write, &written, 0);
        note_speaker_i2s_write(bytes_to_write, written, bytes_per_frame, err);
        if (written > 0)
        {
            const size_t remaining = g_pending_speaker_bytes - written;
            if (remaining > 0)
            {
                memmove(g_pending_speaker_buffer, g_pending_speaker_buffer + written, remaining);
            }
            g_pending_speaker_bytes = remaining;
        }
        if (err != ESP_OK || written < bytes_to_write)
        {
            return_i2s_tx_bytes(bytes_to_write - written);
            if (err != ESP_ERR_TIMEOUT)
            {
                DBG_LOGW(TAG,
                         "speaker i2s pending write failed: %s, wrote %u/%u bytes",
                         esp_err_to_name(err),
                         static_cast<unsigned>(written),
                         static_cast<unsigned>(bytes_to_write));
            }
            return;
        }
    }

    if (!g_fifo_bt2spk.readyToDequeue())
    {
        return;
    }

    const size_t frames_to_write = take_i2s_tx_frames(kPumpSamples);
    if (frames_to_write == 0)
    {
        return;
    }

    const bool   use_mono_output = !speaker_output_stereo();
    const size_t frames          = use_mono_output ? g_fifo_bt2spk.dequeueMono(g_mono_buffer, frames_to_write)
                                                   : g_fifo_bt2spk.dequeueStereo(g_stereo_buffer, frames_to_write);
    if (frames == 0)
    {
        return_i2s_tx_bytes(frames_to_write * speaker_bytes_per_frame());
        return;
    }

    size_t      written          = 0;
    const void* samples_to_write = nullptr;
    size_t      bytes_to_write   = 0;
    if (use_mono_output)
    {
        apply_speaker_software_volume(g_mono_buffer, frames);
        samples_to_write = g_mono_buffer;
        bytes_to_write   = frames * sizeof(int16_t);
    }
    else
    {
        apply_speaker_software_volume(g_stereo_buffer, frames * 2);
        samples_to_write = g_stereo_buffer;
        bytes_to_write   = frames * 2 * sizeof(int16_t);
    }

    const esp_err_t err = i2s_channel_write(g_i2s_tx, samples_to_write, bytes_to_write, &written, 0);
    note_speaker_i2s_write(bytes_to_write, written, speaker_bytes_per_frame(), err);
    if (err != ESP_OK || written < bytes_to_write)
    {
        return_i2s_tx_bytes(bytes_to_write - written);
        if (written < bytes_to_write)
        {
            g_pending_speaker_bytes = bytes_to_write - written;
            memcpy(g_pending_speaker_buffer,
                   reinterpret_cast<const uint8_t*>(samples_to_write) + written,
                   g_pending_speaker_bytes);
        }
        if (err != ESP_ERR_TIMEOUT)
        {
            DBG_LOGW(TAG,
                     "speaker i2s write failed: %s, wrote %u/%u bytes",
                     esp_err_to_name(err),
                     static_cast<unsigned>(written),
                     static_cast<unsigned>(bytes_to_write));
        }
    }
}

void pump_mic2bt()
{
    bool notify_ready = false;

    std::unique_lock<std::mutex> lock(g_pump_mutex, std::try_to_lock);
    if (!lock.owns_lock())
    {
        return;
    }

    HFP_AUDIO_DIAG([](HfpAudioDiagnostics& diag) { ++diag.micPumpCalls; });

    if (!mic_input_active())
    {
        HFP_AUDIO_DIAG([](HfpAudioDiagnostics& diag) { ++diag.micSkipNotMicMode; });
        MicGainManager::process(nullptr, 0);
        return;
    }

    if (!g_i2s_rx)
    {
        HFP_AUDIO_DIAG([](HfpAudioDiagnostics& diag) { ++diag.micSkipNoI2s; });
        MicGainManager::process(nullptr, 0);
        return;
    }

    const bool bt_ready = BtManager::canNotifyOutgoingAudioReady();
    if (!bt_ready)
    {
        g_fifo_mic2bt.clear();
    }
    else if (g_fifo_mic2bt.availableToWrite() == 0)
    {
        HFP_AUDIO_DIAG([](HfpAudioDiagnostics& diag) { ++diag.micSkipFifoFull; });
        notify_ready = should_notify_hfp_outgoing_ready(0, true);
    }

    const bool ext_mic     = g_hardware == Hardware::ExternalI2SCodec;
    size_t     bytes_read  = 0;
    void*      read_buffer = ext_mic ? static_cast<void*>(g_stereo_buffer) : static_cast<void*>(g_mono_buffer);
    size_t     read_bytes  = ext_mic ? sizeof(g_stereo_buffer) : sizeof(g_mono_buffer);

    HFP_AUDIO_DIAG(
        [read_bytes](HfpAudioDiagnostics& diag)
        {
            ++diag.micI2sReadCalls;
            diag.micI2sRequestedBytes += read_bytes;
        });

    const esp_err_t read_err = i2s_channel_read(g_i2s_rx, read_buffer, read_bytes, &bytes_read, 0);
    if (read_err != ESP_OK)
    {
        HFP_AUDIO_DIAG([](HfpAudioDiagnostics& diag) { ++diag.micI2sReadErrors; });
        MicGainManager::process(nullptr, 0);
        return;
    }

    if (bytes_read == 0)
    {
        HFP_AUDIO_DIAG([](HfpAudioDiagnostics& diag) { ++diag.micI2sReadEmpty; });
        MicGainManager::process(nullptr, 0);
        return;
    }

    const size_t samples = bytes_read / sizeof(int16_t);
    if (samples == 0)
    {
        MicGainManager::process(nullptr, 0);
        return;
    }

    Diagnostics::i2s_input_samples(static_cast<uint32_t>(ext_mic ? samples / 2 : samples));

    HFP_AUDIO_DIAG(
        [bytes_read, samples](HfpAudioDiagnostics& diag)
        {
            diag.micI2sReadBytes += bytes_read;
            diag.micI2sReadSamples += samples;
        });

    if (ext_mic)
    {
        const size_t frames = samples / 2;
        if (frames == 0)
        {
            MicGainManager::process(nullptr, 0);
            return;
        }

        MicGainManager::process(g_stereo_buffer, frames * 2);
        size_t queued_bt = 0;
        if (bt_ready && g_fifo_mic2bt.availableToWrite() > 0)
        {
            queued_bt = g_fifo_mic2bt.queueStereo(g_stereo_buffer, frames, kSampleRateHz);
        }
        const size_t queued_file = g_fifo_mic2file.queueStereo(g_stereo_buffer, frames, kSampleRateHz);
        HFP_AUDIO_DIAG(
            [queued_bt, queued_file, frames, bt_ready](HfpAudioDiagnostics& diag)
            {
                diag.micQueuedBtSamples += queued_bt;
                diag.micQueuedFileSamples += queued_file;
                if (!bt_ready)
                {
                    diag.micBtNotReadySamples += frames;
                }
                else if (queued_bt < frames)
                {
                    diag.micBtFifoFullSamples += frames - queued_bt;
                }
            });
        notify_ready = notify_ready || should_notify_hfp_outgoing_ready(queued_bt, true);
    }
    else
    {
        MicGainManager::process(g_mono_buffer, samples);
        size_t queued_bt = 0;
        if (bt_ready && g_fifo_mic2bt.availableToWrite() > 0)
        {
            queued_bt = g_fifo_mic2bt.queue(g_mono_buffer, samples, kSampleRateHz);
        }
        const size_t queued_file = g_fifo_mic2file.queue(g_mono_buffer, samples, kSampleRateHz);
        HFP_AUDIO_DIAG(
            [queued_bt, queued_file, samples, bt_ready](HfpAudioDiagnostics& diag)
            {
                diag.micQueuedBtSamples += queued_bt;
                diag.micQueuedFileSamples += queued_file;
                if (!bt_ready)
                {
                    diag.micBtNotReadySamples += samples;
                }
                else if (queued_bt < samples)
                {
                    diag.micBtFifoFullSamples += samples - queued_bt;
                }
            });
        notify_ready = notify_ready || should_notify_hfp_outgoing_ready(queued_bt, true);
    }

    queue_silence_to_reduce_lag(g_fifo_bt2file, g_fifo_mic2file);

    lock.unlock();
    notify_hfp_outgoing_ready(notify_ready);
}

void pump_task()
{
    pump_bt2spk();
    pump_mic2bt();
}

// -----------------------------------------------------------------------------
// HFP Audio
// -----------------------------------------------------------------------------

// callback from bluetooth
void hfp_incoming_audio(const uint8_t* buf, uint32_t len)
{
    if (!buf || len == 0)
    {
        HFP_AUDIO_DIAG([](HfpAudioDiagnostics& diag) { ++diag.incomingNullOrEmpty; });
        return;
    }

    HFP_AUDIO_DIAG(
        [len](HfpAudioDiagnostics& diag)
        {
            ++diag.incomingCallbacks;
            diag.incomingBytes += len;
        });

    queue_hfp_pcm(buf, len);
}

// callback from bluetooth
// only arrives after esp_hf_client_outgoing_data_ready, which needs to be repeatedly called
uint32_t hfp_outgoing_audio(uint8_t* buf, uint32_t len)
{
    if (!buf || len < sizeof(int16_t))
    {
        HFP_AUDIO_DIAG([](HfpAudioDiagnostics& diag) { ++diag.outgoingNullOrSmall; });
        return 0;
    }

    HFP_AUDIO_DIAG(
        [len](HfpAudioDiagnostics& diag)
        {
            ++diag.outgoingCallbacks;
            diag.outgoingRequestedBytes += len;
        });

    pump_mic2bt();

    const size_t output_samples = len / sizeof(int16_t);
    const size_t read = g_fifo_mic2bt.dequeueMono(reinterpret_cast<int16_t*>(buf), output_samples, g_hfp_rate_hz);
    HFP_AUDIO_DIAG(
        [read, output_samples](HfpAudioDiagnostics& diag)
        {
            diag.outgoingPcmSamplesRead += read;
            diag.outgoingReturnedBytes += read * sizeof(int16_t);
            if (read < output_samples)
            {
                ++diag.outgoingUnderflows;
            }
        });
    pump_mic2bt();
    return static_cast<uint32_t>(read * sizeof(int16_t));
}

bool setHfpAudioFormat(HfpCodec codec, uint32_t sampleRateHz)
{
    if (sampleRateHz != 8000 && sampleRateHz != kSampleRateHz)
    {
        DBG_LOGE(TAG, "unsupported HFP sample rate: %lu", static_cast<unsigned long>(sampleRateHz));
        return false;
    }

    g_hfp_codec   = codec;
    g_hfp_rate_hz = sampleRateHz;

    if (g_hfp_codec == HfpCodec::Msbc)
    {
        if (g_hfp_rate_hz != kSampleRateHz)
        {
            DBG_LOGE(TAG, "mSBC requires %lu Hz audio", static_cast<unsigned long>(kSampleRateHz));
            g_hfp_codec   = HfpCodec::Cvsd;
            g_hfp_rate_hz = 8000;
            return false;
        }

        // ESP-IDF Bluedroid keeps SBC/mSBC inside the Bluetooth stack. The
        // application HFP data callbacks see raw signed 16-bit mono PCM.
        DBG_LOGI(TAG, "HFP mSBC format set: callback PCM, %lu Hz", static_cast<unsigned long>(g_hfp_rate_hz));
        return true;
    }

    DBG_LOGI(TAG, "HFP CVSD format set: callback PCM, %lu Hz", static_cast<unsigned long>(g_hfp_rate_hz));
    return true;
}

HfpCodec hfpAudioCodec()
{
    return g_hfp_codec;
}

uint32_t hfpAudioSampleRateHz()
{
    return g_hfp_rate_hz;
}

HfpAudioDiagnostics hfpAudioDiagnostics()
{
    HfpAudioDiagnostics snapshot = {};
#ifdef ENABLE_HFP_AUDIO_DIAGNOSTICS
    portENTER_CRITICAL(&g_hfp_diag_mux);
    snapshot = g_hfp_diag;
    portEXIT_CRITICAL(&g_hfp_diag_mux);
#endif
    return snapshot;
}

void resetHfpAudioDiagnostics()
{
#ifdef ENABLE_HFP_AUDIO_DIAGNOSTICS
    portENTER_CRITICAL(&g_hfp_diag_mux);
    g_hfp_diag = {};
    portEXIT_CRITICAL(&g_hfp_diag_mux);
#endif
}

// -----------------------------------------------------------------------------
// FIFO Accessors
// -----------------------------------------------------------------------------

AudioFifo& bluetoothToSpeakerFifo()
{
    return g_fifo_bt2spk;
}

AudioFifo& bluetoothToFileFifo()
{
    return g_fifo_bt2file;
}

AudioFifo& micToBluetoothFifo()
{
    return g_fifo_mic2bt;
}

AudioFifo& micToFileFifo()
{
    return g_fifo_mic2file;
}

// -----------------------------------------------------------------------------
// Volume and Mute
// -----------------------------------------------------------------------------

void setVolume(uint8_t volume)
{
    const uint8_t clamped = volume > kMaxVolume ? kMaxVolume : volume;
    if (g_speaker_muted)
    {
        if (clamped > kMinVolume)
        {
            g_speaker_restore_volume = clamped;
        }
        g_volume = kMinVolume;
    }
    else
    {
        g_volume = clamped;
    }
}

void volumeUp()
{
    setVolume(g_volume > kMaxVolume - kVolumeStep ? kMaxVolume : g_volume + kVolumeStep);
}

void volumeDown()
{
    setVolume(g_volume < kVolumeStep ? kMinVolume : g_volume - kVolumeStep);
}

uint8_t volume()
{
    return g_volume;
}

bool setSpeakerMuted(bool muted)
{
    if (g_speaker_muted == muted)
    {
        return true;
    }

    if (muted)
    {
        g_speaker_restore_volume = g_volume > kMinVolume ? g_volume : kMaxVolume;
        g_speaker_muted          = true;
        g_volume                 = kMinVolume;
    }
    else
    {
        g_speaker_muted = false;
        g_volume        = g_speaker_restore_volume;
    }

    return true;
}

bool toggleSpeakerMuted()
{
    return setSpeakerMuted(!g_speaker_muted);
}

void muteSpeaker()
{
    setSpeakerMuted(true);
}

void unmuteSpeaker()
{
    setSpeakerMuted(false);
}

bool speakerMuted()
{
    return g_speaker_muted;
}

void setMicMuted(bool muted)
{
    g_fifo_mic2bt.setMuted(muted);
    g_fifo_mic2file.setMuted(muted);
}

void muteMic()
{
    setMicMuted(true);
}

void unmuteMic()
{
    setMicMuted(false);
}

bool micMuted()
{
    return g_fifo_mic2bt.muted();
}

uint8_t micPeakLevel()
{
    return MicGainManager::rawPeakLevel();
}

uint8_t micScaledPeakLevel()
{
    return MicGainManager::scaledPeakLevel();
}

uint8_t speakerPeakLevel()
{
    return SpeakerPeakActivity::rawPeakLevel();
}

void disconnect_uart0_tx_from_mclk_pin()
{
    if (kSGTL5000I2sMclk != 1)
    {
        return;
    }

    Serial.flush();
    const gpio_num_t gpio = static_cast<gpio_num_t>(kSGTL5000I2sMclk);
    gpio_hal_iomux_func_sel(GPIO_PIN_MUX_REG[gpio], PIN_FUNC_GPIO);
    esp_rom_gpio_connect_out_signal(gpio, SIG_GPIO_OUT_IDX, false, false);
}

void reconnect_uart0_tx()
{
    Serial.end();
    Serial.begin(115200, SERIAL_8N1, -1, 1);
}

bool configure_i2s_shared()
{
    return configure_i2s_shared_impl(kSampleRateHz);
}

namespace
{

// -----------------------------------------------------------------------------
// Feature Logic
// -----------------------------------------------------------------------------

bool begin_fifos()
{
    log_heap_after_fifo_begin("before FIFO allocation", true);

    const bool bt2spk_ok = g_fifo_bt2spk.begin();
    log_heap_after_fifo_begin("g_fifo_bt2spk", bt2spk_ok);

    const bool bt2file_ok = g_fifo_bt2file.begin();
    log_heap_after_fifo_begin("g_fifo_bt2file", bt2file_ok);

    const bool mic2bt_ok = g_fifo_mic2bt.begin();
    log_heap_after_fifo_begin("g_fifo_mic2bt", mic2bt_ok);

    const bool mic2file_ok = g_fifo_mic2file.begin();
    log_heap_after_fifo_begin("g_fifo_mic2file", mic2file_ok);

    if (!bt2spk_ok || !bt2file_ok || !mic2bt_ok || !mic2file_ok)
    {
        DBG_LOGE(TAG, "Audio FIFO allocation failed");
        return false;
    }

    return true;
}

void stop_audio_mode()
{
    std::lock_guard<std::mutex> lock(g_pump_mutex);

    g_mode = P2TMode::Stopped;

    if (g_speaker_path == SpeakerPath::ExternalI2SCodec)
    {
        #ifndef TEST_MOCK_EXT_CODEC
        if (AudioControlSGTL5000* codec = ExtCodec::control())
        {
            codec->muteHeadphone();
        }
        #endif
    }

    if (g_speaker_path == SpeakerPath::NS4168 || g_hardware == Hardware::M5StackInternal)
    {
        set_ns4168_speaker_enabled(false);
    }

    g_speaker_path          = SpeakerPath::None;
    g_pending_speaker_bytes = 0;
}

void stop_i2s()
{
    std::lock_guard<std::mutex> lock(g_pump_mutex);

    g_mode = P2TMode::Stopped;

    if (g_i2s_tx)
    {
        i2s_channel_disable(g_i2s_tx);
        i2s_del_channel(g_i2s_tx);
        g_i2s_tx = nullptr;
    }

    if (g_i2s_rx)
    {
        i2s_channel_disable(g_i2s_rx);
        i2s_del_channel(g_i2s_rx);
        g_i2s_rx = nullptr;
    }

    if (g_speaker_path == SpeakerPath::ExternalI2SCodec)
    {
        #ifndef TEST_MOCK_EXT_CODEC
        if (AudioControlSGTL5000* codec = ExtCodec::control())
        {
            codec->muteHeadphone();
        }
        #endif
    }

    g_speaker_path       = SpeakerPath::None;
    g_i2s_config         = I2sConfig::None;
    g_i2s_sample_rate_hz = 0;
    if (g_hardware == Hardware::M5StackInternal)
    {
        set_ns4168_speaker_enabled(false);
    }

    clear_i2s_tx_credit();
    g_pending_speaker_bytes = 0;

    duplicate_i2s0_bclk_to_gpio13();
}

bool configure_i2s_shared_impl(uint32_t sampleRateHz)
{
    sampleRateHz = speaker_sample_rate_or_default(sampleRateHz);
    if (g_i2s_config == I2sConfig::SharedStd && g_i2s_tx && g_i2s_rx && g_i2s_sample_rate_hz == sampleRateHz)
    {
        duplicate_i2s0_bclk_to_gpio13();
        return true;
    }

    stop_i2s();

    i2s_chan_config_t chan_config = I2S_CHANNEL_DEFAULT_CONFIG(kI2sPort, I2S_ROLE_MASTER);
    chan_config.dma_desc_num      = kDmaBufferCount;
    chan_config.dma_frame_num     = kPumpSamples;
    chan_config.auto_clear        = true;

    i2s_std_config_t config      = {};
    config.clk_cfg               = I2S_STD_CLK_DEFAULT_CONFIG(sampleRateHz);
    config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_512; // only connected to SGTL5000
    config.slot_cfg              = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    config.slot_cfg.slot_mask    = I2S_STD_SLOT_BOTH;
    config.slot_cfg.ws_width     = 16;
    #ifndef USE_LEDC_PWM_AS_MCLK
    config.gpio_cfg.mclk         = static_cast<gpio_num_t>(kSGTL5000I2sMclk);   // only connected to SGTL5000
    #else
    config.gpio_cfg.mclk         = static_cast<gpio_num_t>(-1);
    #endif
    config.gpio_cfg.bclk         = static_cast<gpio_num_t>(kNS4168SpeakerBclk); // shared with duplicate_i2s0_bclk_to_gpio13
    config.gpio_cfg.ws           = static_cast<gpio_num_t>(kNS4168SpeakerLrck); // shared with kSGTL5000I2sLrck
    config.gpio_cfg.dout         = static_cast<gpio_num_t>(kNS4168SpeakerDout); // shared with kSGTL5000I2sDout
    config.gpio_cfg.din          = static_cast<gpio_num_t>(kSGTL5000I2sDin);    // only connected to SGTL5000

    #ifdef USE_LEDC_PWM_AS_MCLK
    ExtCodec::start_ledc_mclk();
    #else
    disconnect_uart0_tx_from_mclk_pin();
    #endif

    if (!ok(i2s_new_channel(&chan_config, &g_i2s_tx, &g_i2s_rx), "shared-i2s full-duplex i2s channel") ||
        !ok(i2s_channel_init_std_mode(g_i2s_tx, &config), "shared-i2s tx i2s std init") ||
        !ok(i2s_channel_init_std_mode(g_i2s_rx, &config), "shared-i2s rx i2s std init") ||
        !register_i2s_callbacks(g_i2s_tx, false) || !register_i2s_callbacks(g_i2s_rx, true) ||
        !preload_silence(g_i2s_tx) || !enable_channel(g_i2s_rx, "shared-i2s rx i2s enable") ||
        !enable_channel(g_i2s_tx, "shared-i2s tx i2s enable"))
    {
        stop_i2s();
        return false;
    }

    duplicate_i2s0_bclk_to_gpio13();
    g_i2s_config         = I2sConfig::SharedStd;
    g_i2s_sample_rate_hz = sampleRateHz;
    return true;
}

bool enable_ns4168_speaker(uint32_t sampleRateHz)
{
    sampleRateHz = speaker_sample_rate_or_default(sampleRateHz);
    if (!configure_i2s_shared_impl(sampleRateHz))
    {
        set_ns4168_speaker_enabled(false);
        return false;
    }

    g_fifo_bt2spk.clear();
    g_fifo_bt2spk.setChoked(false);
    set_ns4168_speaker_enabled(true);

    g_mode         = P2TMode::Speaker;
    g_speaker_path = SpeakerPath::NS4168;
    return true;
}

bool enable_spm1423_mic()
{
    stop_i2s();
    g_fifo_bt2spk.clear();
    g_fifo_bt2spk.setChoked(true);
    set_ns4168_speaker_enabled(false);

    i2s_chan_config_t chan_config = I2S_CHANNEL_DEFAULT_CONFIG(kI2sPort, I2S_ROLE_MASTER);
    chan_config.dma_desc_num      = kDmaBufferCount;
    chan_config.dma_frame_num     = kPumpSamples;

    i2s_pdm_rx_config_t config = {};
    config.clk_cfg             = I2S_PDM_RX_CLK_DEFAULT_CONFIG(kSampleRateHz);
    config.slot_cfg            = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    config.slot_cfg.slot_mask  = I2S_PDM_SLOT_RIGHT;
    config.gpio_cfg.clk        = static_cast<gpio_num_t>(kSPM1423MicClk);
    config.gpio_cfg.din        = static_cast<gpio_num_t>(kSPM1423MicDin);

    if (!ok(i2s_new_channel(&chan_config, nullptr, &g_i2s_rx), "SPM1423 mic pdm channel") ||
        !ok(i2s_channel_init_pdm_rx_mode(g_i2s_rx, &config), "SPM1423 mic pdm init") ||
        !register_i2s_callbacks(g_i2s_rx, true) || !enable_channel(g_i2s_rx, "SPM1423 mic pdm enable"))
    {
        g_fifo_bt2spk.setChoked(false);
        stop_i2s();
        return false;
    }

    MicGainManager::ignoreSamplesFor();
    g_i2s_config         = I2sConfig::InternalPdm;
    g_i2s_sample_rate_hz = kSampleRateHz;
    g_mode               = P2TMode::Mic;
    return true;
}

bool enable_exti2scodec(P2TMode nextMode, uint32_t sampleRateHz)
{
    if (nextMode != P2TMode::Speaker && nextMode != P2TMode::Mic && nextMode != P2TMode::FullDuplex)
    {
        DBG_LOGE(TAG, "invalid ExternalI2SCodec mode request");
        return false;
    }

    sampleRateHz = speaker_sample_rate_or_default(sampleRateHz);
    if (!ensure_exti2scodec_i2s(sampleRateHz) || !sync_external_codec_routing())
    {
        return false;
    }

    if (nextMode == P2TMode::Speaker)
    {
        g_fifo_bt2spk.clear();
        g_fifo_bt2spk.setChoked(false);
    }
    else if (nextMode == P2TMode::Mic)
    {
        g_fifo_bt2spk.clear();
        g_fifo_bt2spk.setChoked(true);
        MicGainManager::ignoreSamplesFor();
    }
    else
    {
        g_fifo_bt2spk.setChoked(false);
        MicGainManager::ignoreSamplesFor();
    }

    g_mode = nextMode;
    set_external_codec_headphone_enabled(nextMode == P2TMode::Speaker || nextMode == P2TMode::FullDuplex);
    DBG_LOGI(TAG,
             "ExternalI2SCodec mode=%u sampleRate=%lu state=%s micInput=%s",
             static_cast<unsigned>(nextMode),
             static_cast<unsigned long>(sampleRateHz),
             ExtCodec::stateName(ExtCodec::state()),
             ExtCodec::micInputName(ExtCodec::micInputForState(ExtCodec::state())));
    return true;
}

bool ensure_exti2scodec_i2s(uint32_t sampleRateHz)
{
    if (!ExtCodec::available())
    {
        DBG_LOGE(TAG, "ExternalI2SCodec requested, but SGTL5000 is unavailable");
        return false;
    }

    sampleRateHz = speaker_sample_rate_or_default(sampleRateHz);

    #ifdef TEST_MOCK_EXT_CODEC
    if (g_speaker_path != SpeakerPath::ExternalI2SCodec || g_i2s_sample_rate_hz != sampleRateHz)
    {
        stop_i2s();
        g_speaker_path       = SpeakerPath::ExternalI2SCodec;
        g_i2s_sample_rate_hz = sampleRateHz;
    }
    return true;
    #endif

    if (!configure_i2s_shared_impl(sampleRateHz))
    {
        return false;
    }

    set_ns4168_speaker_enabled(false);
    g_speaker_path = SpeakerPath::ExternalI2SCodec;
    return true;
}

bool sync_external_codec_routing()
{
    if (!ExtCodec::available())
    {
        return false;
    }

    const ExtCodec::State current = ExtCodec::state();
    if (!ExtCodec::configureAnalogPathForState(current))
    {
        DBG_LOGW(TAG, "failed to configure SGTL5000 analog path for %s", ExtCodec::stateName(current));
        return false;
    }
    return true;
}

void duplicate_i2s0_bclk_to_gpio13()
{
    if (kSGTL5000I2sBclk < 0)
    {
        return;
    }

    const gpio_num_t gpio = static_cast<gpio_num_t>(kSGTL5000I2sBclk);
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    esp_rom_gpio_pad_select_gpio(gpio);
    esp_rom_gpio_connect_out_signal(gpio, I2S0O_BCK_OUT_IDX, false, false);
}

void queue_hfp_pcm(const uint8_t* buf, uint32_t len)
{
    if (len < sizeof(int16_t))
    {
        return;
    }

    const size_t samples = len / sizeof(int16_t);
    const auto*  pcm     = reinterpret_cast<const int16_t*>(buf);
    SpeakerPeakActivity::process(pcm, samples);
    const size_t queued_spk  = g_fifo_bt2spk.queue(pcm, samples, g_hfp_rate_hz);
    const size_t queued_file = g_fifo_bt2file.queue(pcm, samples, g_hfp_rate_hz);

    if (!mic_file_source_active())
    {
        queue_silence_to_match(g_fifo_mic2file, g_fifo_bt2file);
    }
    else
    {
        queue_silence_to_reduce_lag(g_fifo_mic2file, g_fifo_bt2file);
    }

    HFP_AUDIO_DIAG(
        [len, samples, queued_spk, queued_file](HfpAudioDiagnostics& diag)
        {
            diag.incomingConsumedBytes += len;
            diag.incomingPcmSamples += samples;
            diag.incomingQueuedSpkSamples += queued_spk;
            diag.incomingQueuedFileSamples += queued_file;
        });
    pump_bt2spk();
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

bool should_notify_hfp_outgoing_ready(size_t queued_bt, bool allow_existing_fifo)
{
    const size_t readable_samples = g_fifo_mic2bt.availableToRead();
    const bool   queued = queued_bt > 0 || (allow_existing_fifo && readable_samples >= kHfpOutgoingNotifyMinSamples);
    const bool   enough_samples = readable_samples >= kHfpOutgoingNotifyMinSamples;
    const bool   bt_ready       = BtManager::canNotifyOutgoingAudioReady();
    const bool   ready          = queued && enough_samples && bt_ready;

    HFP_AUDIO_DIAG(
        [readable_samples, queued, enough_samples, bt_ready, ready](HfpAudioDiagnostics& diag)
        {
            ++diag.micNotifyChecks;
            diag.micLastReadableSamples = static_cast<uint32_t>(min(readable_samples, static_cast<size_t>(UINT32_MAX)));
            diag.micNotifyMinSamples    = static_cast<uint32_t>(kHfpOutgoingNotifyMinSamples);
            if (!queued)
            {
                ++diag.micNotifyNoQueued;
            }
            if (queued && !enough_samples)
            {
                ++diag.micNotifyBelowMin;
            }
            if (queued && enough_samples && !bt_ready)
            {
                ++diag.micNotifyBtNotReady;
            }
            if (ready)
            {
                ++diag.micNotifyReady;
            }
        });

    return ready;
}

void notify_hfp_outgoing_ready(bool ready)
{
    if (!ready)
    {
        return;
    }

    BtManager::notifyOutgoingAudioReady();
    HFP_AUDIO_DIAG([](HfpAudioDiagnostics& diag) { ++diag.micNotifyCalls; });
}

bool mic_file_source_active()
{
    return mic_input_active() && g_i2s_rx != nullptr;
}

void queue_silence_to_match(AudioFifo& lagging_fifo, AudioFifo& leading_fifo)
{
    const size_t leading = leading_fifo.usedSamples();
    const size_t lagging = lagging_fifo.usedSamples();
    if (leading > lagging)
    {
        lagging_fifo.queueSilence(leading - lagging);
    }
}

void queue_silence_to_reduce_lag(AudioFifo& lagging_fifo, AudioFifo& leading_fifo)
{
    const size_t leading = leading_fifo.usedSamples();
    const size_t lagging = lagging_fifo.usedSamples();
    if (leading <= lagging + kFileFifoCatchupStartSamples)
    {
        return;
    }

    const size_t target_lagging = leading - kFileFifoCatchupTargetSamples;
    if (target_lagging > lagging)
    {
        lagging_fifo.queueSilence(target_lagging - lagging);
    }
}

void set_ns4168_speaker_enabled(bool enabled)
{
    const auto pmic_type = M5.Power.getType();
    if (pmic_type == m5::Power_Class::pmic_axp192)
    {
        M5.Power.Axp192.setGPIO2(enabled);
        return;
    }
    if (pmic_type == m5::Power_Class::pmic_axp2101)
    {
        M5.Power.Axp2101.setALDO3(enabled ? 3300 : 0);
        return;
    }

    if (!g_wire_started)
    {
        Wire.begin(kInternalI2cSda, kInternalI2cScl, 400000);
        g_wire_started = true;
    }

    Wire.beginTransmission(kAxp192Address);
    Wire.write(kAxp192Gpio2Control);
    Wire.write(enabled ? 0x06 : 0x05);
    const uint8_t result = Wire.endTransmission();
    if (result != 0)
    {
        DBG_LOGW(TAG, "AXP192 NS4168 speaker GPIO2 write failed: %u", result);
    }
}

void set_external_codec_headphone_enabled(bool enabled)
{
    if (g_speaker_path != SpeakerPath::ExternalI2SCodec)
    {
        return;
    }

    #ifdef TEST_MOCK_EXT_CODEC
    return;
    #else
    AudioControlSGTL5000* codec = ExtCodec::control();
    if (!codec)
    {
        return;
    }

    if (!(enabled ? codec->unmuteHeadphone() : codec->muteHeadphone()))
    {
        DBG_LOGW(TAG, "failed to %s SGTL5000 headphone output", enabled ? "unmute" : "mute");
    }
    #endif
}

bool using_ns4168_speaker()
{
    return g_speaker_path == SpeakerPath::NS4168;
}

bool speaker_output_stereo()
{
    if (!using_ns4168_speaker())
    {
        return true;
    }

    #ifdef NS4168_USE_STEREO
    return true;
    #else
    return false;
    #endif
}

bool speaker_output_active()
{
    return g_mode == P2TMode::Speaker || g_mode == P2TMode::FullDuplex;
}

bool mic_input_active()
{
    return g_mode == P2TMode::Mic || g_mode == P2TMode::FullDuplex;
}

uint32_t speaker_sample_rate_or_default(uint32_t sampleRateHz)
{
    return sampleRateHz == 0 ? kSampleRateHz : sampleRateHz;
}

size_t speaker_bytes_per_frame()
{
    return speaker_output_stereo() ? 2 * sizeof(int16_t) : sizeof(int16_t);
}

void apply_speaker_software_volume(int16_t* samples, size_t sampleCount)
{
    if (g_volume == kMaxVolume)
    {
        return;
    }

    if (g_volume == kMinVolume)
    {
        memset(samples, 0, sampleCount * sizeof(samples[0]));
        return;
    }

    const uint16_t gain = kVolumeGainByLevel[g_volume - 1];
    for (size_t i = 0; i < sampleCount; ++i)
    {
        samples[i] = static_cast<int16_t>((static_cast<int32_t>(samples[i]) * gain) >> kVolumeGainShift);
    }
}

size_t take_i2s_tx_frames(size_t maxFrames)
{
    const size_t bytes_per_frame = speaker_bytes_per_frame();
    portENTER_CRITICAL(&g_i2s_tx_credit_mux);
    const size_t frames = std::min(maxFrames, static_cast<size_t>(g_i2s_tx_available_bytes) / bytes_per_frame);
    g_i2s_tx_available_bytes -= frames * bytes_per_frame;
    portEXIT_CRITICAL(&g_i2s_tx_credit_mux);
    return frames;
}

void return_i2s_tx_bytes(size_t bytes)
{
    add_i2s_tx_credit(bytes);
}

void add_i2s_tx_credit(size_t bytes)
{
    portENTER_CRITICAL(&g_i2s_tx_credit_mux);
    const size_t available = g_i2s_tx_available_bytes + bytes;
    g_i2s_tx_available_bytes = available > kI2sTxCreditLimitBytes ? kI2sTxCreditLimitBytes : available;
    portEXIT_CRITICAL(&g_i2s_tx_credit_mux);
}

void clear_i2s_tx_credit()
{
    portENTER_CRITICAL(&g_i2s_tx_credit_mux);
    g_i2s_tx_available_bytes = 0;
    portEXIT_CRITICAL(&g_i2s_tx_credit_mux);
}

bool IRAM_ATTR i2s_transfer_ready(i2s_chan_handle_t, i2s_event_data_t* event, void*)
{
    if (event)
    {
        portENTER_CRITICAL_ISR(&g_i2s_tx_credit_mux);
        const size_t available = g_i2s_tx_available_bytes + event->size;
        g_i2s_tx_available_bytes = available > kI2sTxCreditLimitBytes ? kI2sTxCreditLimitBytes : available;
        portEXIT_CRITICAL_ISR(&g_i2s_tx_credit_mux);
    }
    return false;
}

bool IRAM_ATTR i2s_rx_transfer_ready(i2s_chan_handle_t, i2s_event_data_t*, void*)
{
    return false;
}

bool register_i2s_callbacks(i2s_chan_handle_t handle, bool rx)
{
    i2s_event_callbacks_t callbacks = {};
    if (rx)
    {
        callbacks.on_recv = i2s_rx_transfer_ready;
    }
    else
    {
        callbacks.on_sent = i2s_transfer_ready;
    }

    return ok(i2s_channel_register_event_callback(handle, &callbacks, nullptr), "i2s event callback");
}

bool preload_silence(i2s_chan_handle_t handle)
{
    int16_t zeros[kPumpSamples * 2] = {};
    size_t  loaded                  = 0;
    return ok(i2s_channel_preload_data(handle, zeros, sizeof(zeros), &loaded), "speaker i2s preload");
}

bool enable_channel(i2s_chan_handle_t handle, const char* label)
{
    if (!ok(i2s_channel_enable(handle), label))
    {
        stop_i2s();
        return false;
    }
    return true;
}

void note_speaker_i2s_write(size_t requested, size_t written, size_t bytes_per_frame, esp_err_t err)
{
    if (written > 0 && bytes_per_frame > 0)
    {
        Diagnostics::i2s_output_samples(static_cast<uint32_t>(written / bytes_per_frame));
    }

#ifdef ENABLE_HFP_AUDIO_DIAGNOSTICS
    HFP_AUDIO_DIAG(
        [requested, written, bytes_per_frame, err](HfpAudioDiagnostics& diag)
        {
            if (written > 0)
            {
                diag.speakerI2sWriteBytes += written;
                diag.speakerI2sWriteFrames += bytes_per_frame ? written / bytes_per_frame : 0;
                ++diag.speakerPumpCalls;
            }
            if (written < requested)
            {
                ++diag.speakerI2sShortWrites;
            }
            if (err != ESP_OK)
            {
                ++diag.speakerI2sWriteErrors;
            }
        });
#endif
}

// -----------------------------------------------------------------------------
// Debug / Logging Helpers
// -----------------------------------------------------------------------------

void log_heap_after_fifo_begin(const char* fifo_name, bool result)
{
    DBG_LOGI(TAG,
             "%s begin %s: free heap=%u, largest block=%u, internal free=%u, spiram free=%u",
             fifo_name,
             result ? "ok" : "failed",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}

} // namespace

} // namespace AudioManager
