#include <Arduino.h>
#include <M5Unified.h>

#include "AudioManager.h"
#include "ExtCodec.h"
#include "pins.h"

namespace
{

constexpr int32_t  kTextX               = 0;
constexpr int32_t  kTextY               = 0;
constexpr uint8_t  kTextFont            = 1;
constexpr float    kTextSize            = 1.0f;
constexpr uint16_t kSgtl5000PllLockMask = 1U << 4;
constexpr uint32_t kPollDelayMs         = 10;
constexpr uint32_t kHeartbeatPeriodMs   = 1000;

int32_t g_next_line_y = kTextY;
int32_t g_line_height = 8;

struct PllSample
{
    bool     read_ok    = false;
    uint16_t ana_status = 0;
    bool     locked     = false;
};

const char* ok_text(bool ok)
{
    return ok ? "ok" : "fail";
}

void init_screen()
{
    M5.Display.setBrightness(255);
    M5.Display.setColorDepth(16);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextFont(kTextFont);
    M5.Display.setTextSize(kTextSize);
    M5.Display.setTextDatum(top_left);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);

    g_line_height = static_cast<int32_t>(M5.Display.fontHeight());
    if (g_line_height <= 0)
    {
        g_line_height = 8;
    }
    g_next_line_y = kTextY;
}

void write_line(const char* text)
{
    if (g_next_line_y + g_line_height > M5.Display.height())
    {
        g_next_line_y = kTextY;
    }

    M5.Display.fillRect(0, g_next_line_y, M5.Display.width(), g_line_height, TFT_BLACK);
    M5.Display.drawString(text ? text : "", kTextX, g_next_line_y);
    g_next_line_y += g_line_height;
}

PllSample read_pll_sample()
{
    PllSample sample;
    sample.read_ok = ExtCodec::readChipAnaStatus(sample.ana_status);
    sample.locked  = sample.read_ok && ((sample.ana_status & kSgtl5000PllLockMask) != 0);
    return sample;
}

void write_init_result(bool i2s_ok, bool codec_init_ok)
{
    char line[64] = {};
    snprintf(line,
             sizeof(line),
             "i2s=%s codec=%s avail=%u",
             ok_text(i2s_ok),
             ok_text(codec_init_ok),
             ExtCodec::available() ? 1U : 0U);
    write_line(line);
}

void write_chip_ana_status(const char* prefix, const PllSample& sample)
{
    char line[64] = {};
    if (sample.read_ok)
    {
        snprintf(line,
                 sizeof(line),
                 "%s ana=0x%04X pll=%u",
                 prefix ? prefix : "ana",
                 static_cast<unsigned>(sample.ana_status),
                 sample.locked ? 1U : 0U);
    }
    else
    {
        snprintf(line, sizeof(line), "%s ana read fail", prefix ? prefix : "ana");
    }
    write_line(line);
}

void write_heartbeat(const PllSample& sample,
                     uint32_t         read_ok_count,
                     uint32_t         read_fail_count,
                     uint32_t         loss_count)
{
    char line[64] = {};
    if (sample.read_ok)
    {
        snprintf(line,
                 sizeof(line),
                 "hb %lus ok=%lu er=%lu l=%u a=%04X loss=%lu",
                 static_cast<unsigned long>(millis() / 1000U),
                 static_cast<unsigned long>(read_ok_count),
                 static_cast<unsigned long>(read_fail_count),
                 sample.locked ? 1U : 0U,
                 static_cast<unsigned>(sample.ana_status),
                 static_cast<unsigned long>(loss_count));
    }
    else
    {
        snprintf(line,
                 sizeof(line),
                 "hb %lus ok=%lu er=%lu readfail loss=%lu",
                 static_cast<unsigned long>(millis() / 1000U),
                 static_cast<unsigned long>(read_ok_count),
                 static_cast<unsigned long>(read_fail_count),
                 static_cast<unsigned long>(loss_count));
    }
    write_line(line);
}

void write_pll_loss(uint32_t loss_count, const PllSample& sample)
{
    char line[64] = {};
    snprintf(line,
             sizeof(line),
             "PLL loss %lu ms=%lu ana=0x%04X",
             static_cast<unsigned long>(loss_count),
             static_cast<unsigned long>(millis()),
             static_cast<unsigned>(sample.ana_status));
    write_line(line);
}

} // namespace

void test_sgtl5000_pll_lock()
{
    const bool i2s_ok        = AudioManager::configure_i2s_shared();
    const bool codec_init_ok = ExtCodec::init();

    init_screen();
    write_line("test_sgtl5000_pll_lock started");
    write_init_result(i2s_ok, codec_init_ok);

    char mclk_line[64] = {};
    snprintf(mclk_line, sizeof(mclk_line), "LEDC MCLK GPIO%d", kLedcMclkGpio);
    write_line(mclk_line);

    PllSample sample          = read_pll_sample();
    uint32_t  read_ok_count   = sample.read_ok ? 1U : 0U;
    uint32_t  read_fail_count = sample.read_ok ? 0U : 1U;
    uint32_t  loss_count      = 0;
    bool      have_lock_state = sample.read_ok;
    bool      was_locked      = sample.locked;
    uint32_t  last_heartbeat  = millis();

    write_chip_ana_status("init", sample);

    while (true)
    {
        sample = read_pll_sample();
        if (sample.read_ok)
        {
            ++read_ok_count;
            if (have_lock_state && was_locked && !sample.locked)
            {
                ++loss_count;
                write_pll_loss(loss_count, sample);
            }

            was_locked      = sample.locked;
            have_lock_state = true;
        }
        else
        {
            ++read_fail_count;
        }

        const uint32_t now_ms = millis();
        if (static_cast<uint32_t>(now_ms - last_heartbeat) >= kHeartbeatPeriodMs)
        {
            write_heartbeat(sample, read_ok_count, read_fail_count, loss_count);
            last_heartbeat = now_ms;
        }

        delay(kPollDelayMs);
    }
}
