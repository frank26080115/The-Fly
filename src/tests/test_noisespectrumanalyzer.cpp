#include <Arduino.h>
#include <M5Unified.h>

#include <math.h>
#include <string.h>

#include "AudioManager.h"
#include "ExtCodec.h"
#include "utilfuncs.h"

namespace
{

constexpr const char* TAG = "test_noisespectrumanalyzer";

constexpr size_t      kFftSize              = 512;
constexpr size_t      kBinCount             = kFftSize / 2;
constexpr float       kBinSpacingHz         = static_cast<float>(AudioManager::kSampleRateHz) / kFftSize;
constexpr float       kInvFullScale         = 1.0f / 32768.0f;
constexpr float       kSmallScaleFullScale  = 0.01f;
constexpr float       kFullScaleFullScale   = 1.0f;
constexpr uint32_t    kFramePeriodMs        = 33;
constexpr uint32_t    kTextPeriodMs         = 200;
constexpr int32_t     kClearSelectionHeight = 64;
constexpr uint32_t    kCore0PumpDelayMs     = 1;
constexpr uint32_t    kCore0StackSize       = 8192;
constexpr UBaseType_t kCore0Priority        = 2;
constexpr uint16_t    kSelectedColour       = TFT_ORANGE;
constexpr uint16_t    kSmallScaleColour     = TFT_WHITE;
constexpr uint16_t    kFullScaleColour      = TFT_GREEN;

enum class ScaleMode : uint8_t
{
    Small,
    Full,
};

TaskHandle_t g_core0_task = nullptr;
M5Canvas     g_canvas(&M5.Display);
ScaleMode    g_scale_mode       = ScaleMode::Small;
int16_t      g_selected_bin     = -1;
bool         g_text_dirty       = true;
uint32_t     g_next_text_ms     = 0;
uint32_t     g_next_frame_ms    = 0;
float        g_rms_fraction     = 0.0f;
float        g_magnitudes[kBinCount];
float        g_fft_real[kFftSize];
float        g_fft_imag[kFftSize];
int16_t      g_samples[kFftSize];
char         g_text_lines[8][24];

size_t min_size(size_t a, size_t b)
{
    return a < b ? a : b;
}

int32_t clamp_i32(int32_t value, int32_t low, int32_t high)
{
    if (value < low)
    {
        return low;
    }
    if (value > high)
    {
        return high;
    }
    return value;
}

float db_from_fraction(float value)
{
    if (value <= 0.000001f)
    {
        return -120.0f;
    }
    return 20.0f * log10f(value);
}

void logf(const char* message)
{
    Serial.printf("%s: %s\n", TAG, message ? message : "");
}

void show_fatal(const char* message)
{
    logf(message);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextFont(1);
    M5.Display.setTextSize(1.0f);
    M5.Display.setTextDatum(top_left);
    M5.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5.Display.drawString(message ? message : "fatal error", 4, 4);
    idle_forever();
}

void setup_display()
{
    M5.Display.setBrightness(255);
    M5.Display.setColorDepth(16);
    M5.Display.fillScreen(TFT_BLACK);

    g_canvas.setColorDepth(16);
    if (!g_canvas.createSprite(M5.Display.width(), M5.Display.height()))
    {
        show_fatal("M5Canvas allocation failed");
    }

    g_canvas.setTextFont(1);
    g_canvas.setTextSize(1.0f);
    g_canvas.setTextDatum(top_right);
}

void setup_buttons()
{
    M5.update();
    M5.BtnA.setDebounceThresh(20);
    M5.BtnB.setDebounceThresh(20);
    M5.BtnC.setDebounceThresh(20);
}

void setup_audio_fifos()
{
    AudioManager::bluetoothToSpeakerFifo().clear();
    AudioManager::bluetoothToSpeakerFifo().choke();
    AudioManager::bluetoothToFileFifo().clear();
    AudioManager::bluetoothToFileFifo().choke();
    AudioManager::micToBluetoothFifo().clear();
    AudioManager::micToBluetoothFifo().choke();

    AudioFifo& fifo = AudioManager::micToFileFifo();
    fifo.clear();
    fifo.unchoke();
    fifo.unmute();
    fifo.setWatermark(0);
    fifo.setSilenceWhenEmpty(false);
}

bool setup_audio()
{
    const bool i2s_ok   = AudioManager::configure_i2s_shared();
    const bool codec_ok = ExtCodec::init();
    const bool audio_ok = AudioManager::init(AudioManager::Hardware::ExternalI2SCodec);

    AudioManager::forceExternalCodecLineInRightMic(true);
    setup_audio_fifos();

    const bool mic_ok = audio_ok && AudioManager::enableMicMode();
    AudioManager::setMicMuted(false);

    Serial.printf("%s: init i2s=%u codec=%u available=%u audio=%u mic=%u state=%s forced_input=line-in-right\n",
                  TAG,
                  i2s_ok ? 1U : 0U,
                  codec_ok ? 1U : 0U,
                  ExtCodec::available() ? 1U : 0U,
                  audio_ok ? 1U : 0U,
                  mic_ok ? 1U : 0U,
                  ExtCodec::stateName(ExtCodec::state()));

    return i2s_ok && codec_ok && audio_ok && mic_ok;
}

void noise_analyzer_core0_task(void*)
{
    logf("core 0 audio pump started");
    while (true)
    {
        AudioManager::pump_mic2bt();
        vTaskDelay(pdMS_TO_TICKS(kCore0PumpDelayMs));
    }
}

bool start_core0_audio_task()
{
    const BaseType_t created = xTaskCreatePinnedToCore(noise_analyzer_core0_task,
                                                       "noise_fft_audio",
                                                       kCore0StackSize,
                                                       nullptr,
                                                       kCore0Priority,
                                                       &g_core0_task,
                                                       0);
    return created == pdPASS;
}

void discard_from_fifo(AudioFifo& fifo, size_t sample_count)
{
    int16_t discard[128];
    while (sample_count > 0)
    {
        const size_t chunk = min_size(sample_count, sizeof(discard) / sizeof(discard[0]));
        const size_t got   = fifo.dequeueMonoImmediate(discard, chunk, AudioManager::kSampleRateHz);
        if (got == 0)
        {
            return;
        }
        sample_count -= got;
    }
}

bool read_latest_samples()
{
    AudioFifo& fifo = AudioManager::micToFileFifo();
    while (fifo.usedSamples() > kFftSize)
    {
        discard_from_fifo(fifo, fifo.usedSamples() - kFftSize);
    }

    if (fifo.usedSamples() < kFftSize)
    {
        return false;
    }

    size_t read = 0;
    while (read < kFftSize)
    {
        const size_t got = fifo.dequeueMonoImmediate(g_samples + read, kFftSize - read, AudioManager::kSampleRateHz);
        if (got == 0)
        {
            return false;
        }
        read += got;
    }

    return true;
}

void bit_reverse_fft_buffers()
{
    size_t j = 0;
    for (size_t i = 1; i < kFftSize; ++i)
    {
        size_t bit = kFftSize >> 1;
        while ((j & bit) != 0)
        {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;

        if (i < j)
        {
            const float real = g_fft_real[i];
            const float imag = g_fft_imag[i];
            g_fft_real[i]    = g_fft_real[j];
            g_fft_imag[i]    = g_fft_imag[j];
            g_fft_real[j]    = real;
            g_fft_imag[j]    = imag;
        }
    }
}

void run_fft()
{
    constexpr float kPi = 3.14159265358979323846f;

    bit_reverse_fft_buffers();
    for (size_t len = 2; len <= kFftSize; len <<= 1)
    {
        const float angle = -2.0f * kPi / static_cast<float>(len);
        const float wlen_real = cosf(angle);
        const float wlen_imag = sinf(angle);

        for (size_t i = 0; i < kFftSize; i += len)
        {
            float w_real = 1.0f;
            float w_imag = 0.0f;
            for (size_t j = 0; j < len / 2; ++j)
            {
                const size_t even = i + j;
                const size_t odd  = even + len / 2;
                const float  u_r  = g_fft_real[even];
                const float  u_i  = g_fft_imag[even];
                const float  v_r  = (g_fft_real[odd] * w_real) - (g_fft_imag[odd] * w_imag);
                const float  v_i  = (g_fft_real[odd] * w_imag) + (g_fft_imag[odd] * w_real);

                g_fft_real[even] = u_r + v_r;
                g_fft_imag[even] = u_i + v_i;
                g_fft_real[odd]  = u_r - v_r;
                g_fft_imag[odd]  = u_i - v_i;

                const float next_w_real = (w_real * wlen_real) - (w_imag * wlen_imag);
                const float next_w_imag = (w_real * wlen_imag) + (w_imag * wlen_real);
                w_real = next_w_real;
                w_imag = next_w_imag;
            }
        }
    }
}

void analyze_samples()
{
    double sum_sq = 0.0;
    for (size_t i = 0; i < kFftSize; ++i)
    {
        const float sample = static_cast<float>(g_samples[i]) * kInvFullScale;
        g_fft_real[i] = sample;
        g_fft_imag[i] = 0.0f;
        sum_sq += static_cast<double>(sample) * sample;
    }

    g_rms_fraction = sqrtf(static_cast<float>(sum_sq / kFftSize));
    run_fft();

    g_magnitudes[0] = fabsf(g_fft_real[0]) / static_cast<float>(kFftSize);
    for (size_t i = 1; i < kBinCount; ++i)
    {
        const float mag = sqrtf((g_fft_real[i] * g_fft_real[i]) + (g_fft_imag[i] * g_fft_imag[i]));
        g_magnitudes[i] = mag / static_cast<float>(kFftSize / 2);
    }
}

const char* scale_name()
{
    return g_scale_mode == ScaleMode::Small ? "SM scale" : "Full scale";
}

float scale_full_fraction()
{
    return g_scale_mode == ScaleMode::Small ? kSmallScaleFullScale : kFullScaleFullScale;
}

uint16_t scale_colour()
{
    return g_scale_mode == ScaleMode::Small ? kSmallScaleColour : kFullScaleColour;
}

void update_text_lines()
{
    snprintf(g_text_lines[0], sizeof(g_text_lines[0]), "%s", scale_name());
    snprintf(g_text_lines[1], sizeof(g_text_lines[1]), "RMS %.2f%%", static_cast<double>(g_rms_fraction * 100.0f));
    snprintf(g_text_lines[2], sizeof(g_text_lines[2]), "%.1f dBFS", static_cast<double>(db_from_fraction(g_rms_fraction)));
    snprintf(g_text_lines[3], sizeof(g_text_lines[3]), "");

    if (g_selected_bin >= 0 && g_selected_bin < static_cast<int16_t>(kBinCount))
    {
        const float frequency = static_cast<float>(g_selected_bin) * kBinSpacingHz;
        const float mag       = g_magnitudes[g_selected_bin];
        snprintf(g_text_lines[4], sizeof(g_text_lines[4]), "bin %u", static_cast<unsigned>(g_selected_bin));
        snprintf(g_text_lines[5], sizeof(g_text_lines[5]), "%.1f Hz", static_cast<double>(frequency));
        snprintf(g_text_lines[6], sizeof(g_text_lines[6]), "mag %.2f%%", static_cast<double>(mag * 100.0f));
        snprintf(g_text_lines[7], sizeof(g_text_lines[7]), "%.1f dB", static_cast<double>(db_from_fraction(mag)));
    }
    else
    {
        snprintf(g_text_lines[4], sizeof(g_text_lines[4]), "bin none");
        g_text_lines[5][0] = '\0';
        g_text_lines[6][0] = '\0';
        g_text_lines[7][0] = '\0';
    }

    g_text_dirty = false;
}

void maybe_update_text()
{
    const uint32_t now = millis();
    if (g_text_dirty || static_cast<int32_t>(now - g_next_text_ms) >= 0)
    {
        update_text_lines();
        g_next_text_ms = now + kTextPeriodMs;
    }
}

void draw_right_text_line(const char* text, int32_t y, uint16_t colour)
{
    if (!text || text[0] == '\0')
    {
        return;
    }

    g_canvas.setTextFont(1);
    g_canvas.setTextSize(1.0f);
    g_canvas.setTextDatum(top_right);
    g_canvas.setTextColor(colour, TFT_BLACK);

    const int32_t x       = g_canvas.width() - 1;
    const int32_t width   = g_canvas.textWidth(text);
    const int32_t height  = g_canvas.fontHeight();
    const int32_t clear_x = clamp_i32(x - width - 2, 0, g_canvas.width() - 1);
    g_canvas.fillRect(clear_x, y, g_canvas.width() - clear_x, height, TFT_BLACK);
    g_canvas.drawString(text, x, y);
}

void draw_text()
{
    const int32_t line_height = g_canvas.fontHeight() + 1;
    int32_t       y           = 2;
    for (size_t i = 0; i < sizeof(g_text_lines) / sizeof(g_text_lines[0]); ++i)
    {
        const uint16_t colour = i >= 4 ? kSelectedColour : TFT_WHITE;
        draw_right_text_line(g_text_lines[i], y, colour);
        y += line_height;
    }
}

void draw_graph()
{
    const int32_t graph_height = g_canvas.height();
    const float   full_scale   = scale_full_fraction();
    const uint16_t colour      = scale_colour();

    g_canvas.fillScreen(TFT_BLACK);
    for (size_t i = 0; i < kBinCount; ++i)
    {
        int32_t height = static_cast<int32_t>((g_magnitudes[i] / full_scale) * graph_height + 0.5f);
        height = clamp_i32(height, 0, graph_height);
        if (height == 0 && g_magnitudes[i] > 0.0f)
        {
            height = 1;
        }

        const uint16_t bin_colour = static_cast<int16_t>(i) == g_selected_bin ? kSelectedColour : colour;
        if (height > 0)
        {
            g_canvas.drawFastVLine(static_cast<int32_t>(i), graph_height - height, height, bin_colour);
        }
    }
    g_canvas.drawFastVLine(static_cast<int32_t>(kBinCount), 0, graph_height, TFT_DARKGREY);
}

void draw_frame()
{
    maybe_update_text();
    draw_graph();
    draw_text();
    g_canvas.pushSprite(0, 0);
}

void select_adjacent(int direction)
{
    if (g_selected_bin < 0)
    {
        return;
    }
    g_selected_bin = static_cast<int16_t>(clamp_i32(g_selected_bin + direction, 0, static_cast<int32_t>(kBinCount - 1)));
    g_text_dirty   = true;
}

void handle_input()
{
    M5.update();

    if (M5.BtnB.wasPressed())
    {
        g_scale_mode = g_scale_mode == ScaleMode::Small ? ScaleMode::Full : ScaleMode::Small;
        g_text_dirty = true;
    }
    if (M5.BtnA.wasPressed())
    {
        select_adjacent(-1);
    }
    if (M5.BtnC.wasPressed())
    {
        select_adjacent(1);
    }

    const auto touch = M5.Touch.getDetail();
    if (touch.wasPressed())
    {
        if (touch.x >= 0 && touch.x < static_cast<int32_t>(kBinCount) && touch.y >= 0 && touch.y < M5.Display.height())
        {
            g_selected_bin = static_cast<int16_t>(touch.x);
            g_text_dirty   = true;
        }
        else if (touch.x >= static_cast<int32_t>(kBinCount) && touch.y >= 0 && touch.y < kClearSelectionHeight)
        {
            g_selected_bin = -1;
            g_text_dirty   = true;
        }
    }
}

void wait_for_next_frame()
{
    const uint32_t now = millis();
    if (static_cast<int32_t>(now - g_next_frame_ms) < 0)
    {
        vTaskDelay(pdMS_TO_TICKS(1));
        return;
    }

    g_next_frame_ms = now + kFramePeriodMs;
}

} // namespace

void test_noisespectrumanalyzer()
{
    Serial.begin(115200, SERIAL_8N1, -1, 1);
    delay(250);

    auto cfg         = M5.config();
    cfg.internal_spk = false;
    cfg.internal_mic = false;
    M5.begin(cfg);
    M5.Speaker.end();
    M5.Mic.end();

    setup_display();
    setup_buttons();

    logf("starting noise spectrum analyzer");
    if (!setup_audio())
    {
        show_fatal("audio init failed");
    }
    if (!start_core0_audio_task())
    {
        show_fatal("core0 task failed");
    }

    update_text_lines();
    while (true)
    {
        handle_input();
        if (static_cast<int32_t>(millis() - g_next_frame_ms) >= 0 && read_latest_samples())
        {
            analyze_samples();
            draw_frame();
        }
        wait_for_next_frame();
    }
}
