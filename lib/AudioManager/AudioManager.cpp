#include "AudioManager.h"

#include <M5Unified.h>
#include <Wire.h>
#include <algorithm>
#include <mutex>
#include <string.h>

#include "AudioFileRecorder.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "sbc.h"
#include "utilfuncs.h"

namespace AudioManager
{
namespace
{

constexpr const char* TAG = "AudioManager";

enum class SpeakerPath
{
    None,
    NS4168,
    WM8960,
};

constexpr i2s_port_t kI2sPort = I2S_NUM_0;

constexpr int kNS4168SpeakerBclk = 12;
constexpr int kNS4168SpeakerLrck = 0;
constexpr int kNS4168SpeakerDout = 2;
constexpr int kSPM1423MicClk     = 0;
constexpr int kSPM1423MicDin     = 34;

constexpr uint8_t kAxp192Address      = 0x34;
constexpr uint8_t kAxp192Gpio2Control = 0x93;
constexpr int     kInternalI2cSda     = 21;
constexpr int     kInternalI2cScl     = 22;

constexpr size_t   kFifoCapacitySamples           = 8192;
constexpr size_t   kFifoWatermarkSamples          = 240;
constexpr size_t   kPumpSamples                   = 240;
constexpr size_t   kDmaBufferCount                = 8;
constexpr uint8_t  kVolumeStep                    = 3;
constexpr uint8_t  kVolumeGainShift               = 10;
constexpr size_t   kSbcScratchBytes               = 512;
constexpr uint16_t kVolumeGainByLevel[kMaxVolume] = {
    // gain = 10^(dB / 20) ; -50 dB was used to generate this table
    3, 4, 5, 6, 7, 9, 11, 13, 16, 19, 24, 29, 35, 43, 52, 64, 78, 95, 115, 141, 172, 209, 255, 311, 380, 463, 565, 688, 840, 1024,
};

AudioFifo g_fifo_bt2spk(kFifoCapacitySamples, kFifoWatermarkSamples);
AudioFifo g_fifo_bt2file(kFifoCapacitySamples, 0);
AudioFifo g_fifo_mic2bt(kFifoCapacitySamples, kFifoWatermarkSamples);
AudioFifo g_fifo_mic2file(kFifoCapacitySamples, 0);

Hardware          g_hardware               = Hardware::M5StackInternal;
P2TMode           g_mode                   = P2TMode::Stopped;
SpeakerPath       g_speaker_path           = SpeakerPath::None;
i2s_chan_handle_t g_i2s_tx                 = nullptr;
i2s_chan_handle_t g_i2s_rx                 = nullptr;
uint8_t           g_volume                 = kMaxVolume;
bool              g_initialized            = false;
bool              g_wire_started           = false;
HfpCodec          g_hfp_codec              = HfpCodec::Msbc;
uint32_t          g_hfp_rate_hz            = kSampleRateHz;
bool              g_sbc_decoder_ready      = false;
bool              g_sbc_encoder_ready      = false;
volatile size_t   g_i2s_tx_available_bytes = 0;
portMUX_TYPE      g_i2s_tx_credit_mux      = portMUX_INITIALIZER_UNLOCKED;
std::mutex        g_pump_mutex;
size_t            g_pending_speaker_bytes = 0;

int16_t g_mono_buffer[kPumpSamples];
int16_t g_stereo_buffer[kPumpSamples * 2];
uint8_t g_pending_speaker_buffer[kPumpSamples * 2 * sizeof(int16_t)];
uint8_t g_sbc_decode_pcm[kSbcScratchBytes];
uint8_t g_sbc_encode_pcm[kSbcScratchBytes];
sbc_t   g_sbc_decoder = {};
sbc_t   g_sbc_encoder = {};

void set_ns4168_speaker_enabled(bool enabled)
{
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
        ESP_LOGW(TAG, "AXP192 NS4168 speaker GPIO2 write failed: %u", result);
    }
}

void update_hardware_volume()
{
    if (g_speaker_path != SpeakerPath::WM8960)
    {
        return;
    }

    // TODO: Apply g_volume to WM8960 once its codec control path is wired up.
    ESP_LOGD(TAG, "volume set to %u/%u", g_volume, kMaxVolume);
}

bool using_ns4168_speaker()
{
    return g_speaker_path == SpeakerPath::NS4168;
}

size_t speaker_bytes_per_frame()
{
    return using_ns4168_speaker() ? sizeof(int16_t) : 2 * sizeof(int16_t);
}

void apply_ns4168_software_volume(int16_t* samples, size_t sampleCount)
{
    if (!using_ns4168_speaker() || g_volume == kMaxVolume)
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

void finish_sbc()
{
    if (g_sbc_decoder_ready)
    {
        sbc_finish(&g_sbc_decoder);
        g_sbc_decoder_ready = false;
    }

    if (g_sbc_encoder_ready)
    {
        sbc_finish(&g_sbc_encoder);
        g_sbc_encoder_ready = false;
    }
}

bool configure_sbc_struct(sbc_t& sbc)
{
    memset(&sbc, 0, sizeof(sbc));
    if (sbc_init(&sbc, 0) != 0)
    {
        return false;
    }

    sbc.frequency  = SBC_FREQ_16000;
    sbc.mode       = SBC_MODE_MONO;
    sbc.allocation = SBC_AM_LOUDNESS;
    sbc.subbands   = SBC_SB_8;
    sbc.blocks     = SBC_BLK_15;
    sbc.bitpool    = 26;
    sbc.endian     = SBC_LE;
    return true;
}

bool init_sbc()
{
    finish_sbc();

    g_sbc_decoder_ready = configure_sbc_struct(g_sbc_decoder);
    g_sbc_encoder_ready = configure_sbc_struct(g_sbc_encoder);
    if (!g_sbc_decoder_ready || !g_sbc_encoder_ready)
    {
        ESP_LOGE(TAG, "libsbc initialization failed");
        finish_sbc();
        return false;
    }

    ESP_LOGI(TAG, "libsbc initialized: frame=%u bytes, pcm=%u bytes", static_cast<unsigned>(sbc_get_frame_length(&g_sbc_encoder)), static_cast<unsigned>(sbc_get_codesize(&g_sbc_encoder)));
    return true;
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
    portENTER_CRITICAL(&g_i2s_tx_credit_mux);
    g_i2s_tx_available_bytes += bytes;
    portEXIT_CRITICAL(&g_i2s_tx_credit_mux);
}

bool IRAM_ATTR i2s_transfer_ready(i2s_chan_handle_t, i2s_event_data_t* event, void*)
{
    if (event)
    {
        portENTER_CRITICAL_ISR(&g_i2s_tx_credit_mux);
        /*
        we don't know how many samples we are allowed to send to i2s_channel_write
        but we know that when a transfer is complete, we have some room
        so we track how much room we have based on how much room is freed by a completed transfer
        */
        g_i2s_tx_available_bytes += event->size;
        portEXIT_CRITICAL_ISR(&g_i2s_tx_credit_mux);
    }
    return false;
}

bool IRAM_ATTR i2s_rx_transfer_ready(i2s_chan_handle_t, i2s_event_data_t*, void*)
{
    return false;
}

void stop_i2s()
{
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

    g_mode         = P2TMode::Stopped;
    g_speaker_path = SpeakerPath::None;
    if (g_hardware == Hardware::M5StackInternal)
    {
        set_ns4168_speaker_enabled(false);
    }

    portENTER_CRITICAL(&g_i2s_tx_credit_mux);
    g_i2s_tx_available_bytes = 0;
    portEXIT_CRITICAL(&g_i2s_tx_credit_mux);
    g_pending_speaker_bytes = 0;
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

bool enable_ns4168_speaker()
{
    stop_i2s();
    set_ns4168_speaker_enabled(true);

    i2s_chan_config_t chan_config = I2S_CHANNEL_DEFAULT_CONFIG(kI2sPort, I2S_ROLE_MASTER);
    chan_config.dma_desc_num      = kDmaBufferCount;
    chan_config.dma_frame_num     = kPumpSamples;
    chan_config.auto_clear        = true;

    i2s_std_config_t config = {};
    config.clk_cfg          = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRateHz);
    config.slot_cfg         = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    config.gpio_cfg.mclk    = I2S_GPIO_UNUSED;
    config.gpio_cfg.bclk    = static_cast<gpio_num_t>(kNS4168SpeakerBclk);
    config.gpio_cfg.ws      = static_cast<gpio_num_t>(kNS4168SpeakerLrck);
    config.gpio_cfg.dout    = static_cast<gpio_num_t>(kNS4168SpeakerDout);
    config.gpio_cfg.din     = I2S_GPIO_UNUSED;

    if (!ok(i2s_new_channel(&chan_config, &g_i2s_tx, nullptr), "NS4168 speaker i2s channel") || !ok(i2s_channel_init_std_mode(g_i2s_tx, &config), "NS4168 speaker i2s std init") || !register_i2s_callbacks(g_i2s_tx, false) || !preload_silence(g_i2s_tx) || !enable_channel(g_i2s_tx, "NS4168 speaker i2s enable"))
    {
        stop_i2s();
        return false;
    }

    g_mode         = P2TMode::Speaker;
    g_speaker_path = SpeakerPath::NS4168;
    return true;
}

bool enable_spm1423_mic()
{
    stop_i2s();
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

    if (!ok(i2s_new_channel(&chan_config, nullptr, &g_i2s_rx), "SPM1423 mic pdm channel") || !ok(i2s_channel_init_pdm_rx_mode(g_i2s_rx, &config), "SPM1423 mic pdm init") || !register_i2s_callbacks(g_i2s_rx, true) || !enable_channel(g_i2s_rx, "SPM1423 mic pdm enable"))
    {
        stop_i2s();
        return false;
    }

    g_mode = P2TMode::Mic;
    return true;
}

bool enable_wm8960_speaker()
{
    ESP_LOGE(TAG, "WM8960 I2S pin/codec setup is not implemented yet");
    return false;
}

bool enable_wm8960_mic()
{
    ESP_LOGE(TAG, "WM8960 I2S pin/codec setup is not implemented yet");
    return false;
}

} // namespace

bool init(Hardware hardware)
{
    g_hardware = hardware;

    auto cfg                   = M5.config();
    cfg.internal_spk           = hardware == Hardware::M5StackInternal;
    cfg.internal_mic           = hardware == Hardware::M5StackInternal;
    cfg.external_speaker_value = 0;
    M5.begin(cfg);
    M5.Speaker.end();
    M5.Mic.end();

    g_fifo_bt2spk.clear();
    g_fifo_bt2file.clear();
    g_fifo_mic2bt.clear();
    g_fifo_mic2file.clear();
    g_fifo_mic2bt.setMuted(false);
    g_fifo_mic2file.setMuted(false);

    AudioFileRecorder::init(g_fifo_bt2file, g_fifo_mic2file);

    g_initialized = true;
    return true;
}

void stop()
{
    stop_i2s();
}

bool enableSpeakerMode()
{
    if (!g_initialized)
    {
        init(g_hardware);
    }

    return g_hardware == Hardware::M5StackInternal ? enable_ns4168_speaker() : enable_wm8960_speaker();
}

bool enableMicMode()
{
    if (!g_initialized)
    {
        init(g_hardware);
    }

    return g_hardware == Hardware::M5StackInternal ? enable_spm1423_mic() : enable_wm8960_mic();
}

P2TMode mode()
{
    return g_mode;
}

void pump_bt2spk()
{
    std::lock_guard<std::mutex> lock(g_pump_mutex);

    if (g_mode != P2TMode::Speaker || !g_i2s_tx)
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
                ESP_LOGW(TAG, "speaker i2s pending write failed: %s, wrote %u/%u bytes", esp_err_to_name(err), static_cast<unsigned>(written), static_cast<unsigned>(bytes_to_write));
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

    const bool   use_mono_output = using_ns4168_speaker();
    const size_t frames          = use_mono_output ? g_fifo_bt2spk.dequeueMono(g_mono_buffer, frames_to_write) : g_fifo_bt2spk.dequeueStereo(g_stereo_buffer, frames_to_write);
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
        apply_ns4168_software_volume(g_mono_buffer, frames);
        samples_to_write = g_mono_buffer;
        bytes_to_write   = frames * sizeof(int16_t);
    }
    else
    {
        samples_to_write = g_stereo_buffer;
        bytes_to_write   = frames * 2 * sizeof(int16_t);
    }

    const esp_err_t err = i2s_channel_write(g_i2s_tx, samples_to_write, bytes_to_write, &written, 0);
    if (err != ESP_OK || written < bytes_to_write)
    {
        return_i2s_tx_bytes(bytes_to_write - written);
        if (written < bytes_to_write)
        {
            g_pending_speaker_bytes = bytes_to_write - written;
            memcpy(g_pending_speaker_buffer, reinterpret_cast<const uint8_t*>(samples_to_write) + written, g_pending_speaker_bytes);
        }
        if (err != ESP_ERR_TIMEOUT)
        {
            ESP_LOGW(TAG, "speaker i2s write failed: %s, wrote %u/%u bytes", esp_err_to_name(err), static_cast<unsigned>(written), static_cast<unsigned>(bytes_to_write));
        }
    }
}

void pump_mic2bt()
{
    if (g_mode != P2TMode::Mic || !g_i2s_rx || g_fifo_mic2bt.availableToWrite() == 0)
    {
        return;
    }

    size_t bytes_read = 0;
    if (i2s_channel_read(g_i2s_rx, g_mono_buffer, sizeof(g_mono_buffer), &bytes_read, 0) != ESP_OK || bytes_read == 0)
    {
        return;
    }

    const size_t samples = bytes_read / sizeof(g_mono_buffer[0]);
    if (samples == 0)
    {
        return;
    }

    g_fifo_mic2bt.queue(g_mono_buffer, samples, kSampleRateHz);
    g_fifo_mic2file.queue(g_mono_buffer, samples, kSampleRateHz);
}

void pump_task()
{
    pump_bt2spk();
    pump_mic2bt();
}

void hfp_incoming_audio(const uint8_t* buf, uint32_t len)
{
    if (!buf || len == 0)
    {
        return;
    }

    if (g_hfp_codec == HfpCodec::Msbc)
    {
        if (!g_sbc_decoder_ready)
        {
            return;
        }

        size_t pos = 0;
        while (pos < len)
        {
            size_t        pcm_written = 0;
            const ssize_t consumed    = sbc_decode(&g_sbc_decoder, buf + pos, len - pos, g_sbc_decode_pcm, sizeof(g_sbc_decode_pcm), &pcm_written);
            if (consumed <= 0 || pcm_written == 0)
            {
                break;
            }

            const auto*  pcm     = reinterpret_cast<const int16_t*>(g_sbc_decode_pcm);
            const size_t samples = pcm_written / sizeof(int16_t);
            g_fifo_bt2spk.queue(pcm, samples, g_hfp_rate_hz);
            g_fifo_bt2file.queue(pcm, samples, g_hfp_rate_hz);
            pos += static_cast<size_t>(consumed);
        }

        pump_bt2spk();
        return;
    }

    if (len < sizeof(int16_t))
    {
        return;
    }

    const size_t samples = len / sizeof(int16_t);
    const auto*  pcm     = reinterpret_cast<const int16_t*>(buf);
    g_fifo_bt2spk.queue(pcm, samples, g_hfp_rate_hz);
    g_fifo_bt2file.queue(pcm, samples, g_hfp_rate_hz);
    pump_bt2spk();
}

uint32_t hfp_outgoing_audio(uint8_t* buf, uint32_t len)
{
    if (!buf || len < sizeof(int16_t))
    {
        return 0;
    }

    pump_mic2bt();

    if (g_hfp_codec == HfpCodec::Msbc)
    {
        if (!g_sbc_encoder_ready)
        {
            return 0;
        }

        const size_t codesize = sbc_get_codesize(&g_sbc_encoder);
        if (codesize == 0 || codesize > sizeof(g_sbc_encode_pcm))
        {
            return 0;
        }

        const size_t samples_to_read = codesize / sizeof(int16_t);
        const size_t read            = g_fifo_mic2bt.dequeueMono(reinterpret_cast<int16_t*>(g_sbc_encode_pcm), samples_to_read);
        if (read < samples_to_read)
        {
            return 0;
        }

        ssize_t       encoded  = 0;
        const ssize_t consumed = sbc_encode(&g_sbc_encoder, g_sbc_encode_pcm, codesize, buf, len, &encoded);
        if (consumed <= 0 || encoded <= 0)
        {
            return 0;
        }

        pump_mic2bt();
        return static_cast<uint32_t>(encoded);
    }

    const size_t output_samples = len / sizeof(int16_t);
    const size_t read           = g_fifo_mic2bt.dequeueMono(reinterpret_cast<int16_t*>(buf), output_samples, g_hfp_rate_hz);
    pump_mic2bt();
    return static_cast<uint32_t>(read * sizeof(int16_t));
}

bool setHfpAudioFormat(HfpCodec codec, uint32_t sampleRateHz)
{
    if (sampleRateHz != 8000 && sampleRateHz != kSampleRateHz)
    {
        ESP_LOGE(TAG, "unsupported HFP sample rate: %lu", static_cast<unsigned long>(sampleRateHz));
        return false;
    }

    g_hfp_codec   = codec;
    g_hfp_rate_hz = sampleRateHz;

    if (g_hfp_codec == HfpCodec::Msbc)
    {
        if (g_hfp_rate_hz != kSampleRateHz)
        {
            ESP_LOGE(TAG, "mSBC requires %lu Hz audio", static_cast<unsigned long>(kSampleRateHz));
            g_hfp_codec   = HfpCodec::Cvsd;
            g_hfp_rate_hz = 8000;
            finish_sbc();
            return false;
        }
        return init_sbc();
    }

    finish_sbc();
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

void setVolume(uint8_t volume)
{
    g_volume = volume > kMaxVolume ? kMaxVolume : volume;
    update_hardware_volume();
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

} // namespace AudioManager
