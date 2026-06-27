#include "MasterTestView.h"

#include <Arduino.h>
#include <string.h>

#include "ExtCodec.h"
#include "HapticsWrapper.h"

namespace
{

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

constexpr uint32_t kRefreshPeriodMs   = 33;
constexpr uint32_t kSineFrequencyHz   = 440;
constexpr size_t   kSineTableSize     = 64;
constexpr size_t   kSineChunkSamples  = 240;
constexpr size_t   kSineTargetSamples = AUDIOFIFO_MS_TO_SAMPLES_16K(120);
constexpr size_t   kSineMaxChunks     = 4;
constexpr uint32_t kPhaseScale        = 65536;
constexpr uint32_t kPhaseStep =
    static_cast<uint32_t>((static_cast<uint64_t>(kSineFrequencyHz) * kSineTableSize * kPhaseScale +
                           (AudioManager::kSampleRateHz / 2U)) /
                          AudioManager::kSampleRateHz);
constexpr int16_t  kCellInset        = 2;
constexpr int16_t  kButtonTextFont   = 2;
constexpr int16_t  kStatusTextFont   = 1;
constexpr int16_t  kButtonLineHeight = 18;
constexpr int16_t  kStatusLineHeight = 10;
constexpr uint16_t kGridColor        = TFT_DARKGREY;
constexpr uint16_t kActiveColor      = TFT_RED;
constexpr uint16_t kBarColor         = TFT_GREEN;

constexpr int16_t kSineTable[kSineTableSize] = {
    0,     803,   1598,  2378,  3135,  3861,  4551,  5196,  5792,  6332,  6811,  7224,  7567,
    7838,  8034,  8152,  8191,  8152,  8034,  7838,  7567,  7224,  6811,  6332,  5792,  5196,
    4551,  3861,  3135,  2378,  1598,  803,   0,     -803,  -1598, -2378, -3135, -3861, -4551,
    -5196, -5792, -6332, -6811, -7224, -7567, -7838, -8034, -8152, -8191, -8152, -8034, -7838,
    -7567, -7224, -6811, -6332, -5792, -5196, -4551, -3861, -3135, -2378, -1598, -803,
};

// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------

enum class Cell
{
    None,
    Status,
    EarbudOutput,
    InternalSpeaker,
    MicHistory,
    EarbudMic,
    InternalMic,
};

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static Cell     cell_at(int16_t x, int16_t y);
static int16_t  cell_x(uint8_t col);
static int16_t  cell_y(uint8_t row);
static int16_t  cell_w(uint8_t col);
static int16_t  cell_h(uint8_t row);
static void     draw_cell_base(uint8_t col, uint8_t row);
static void     draw_toggle_cell(uint8_t col, uint8_t row, const char* label, bool active);
static void     draw_centered_lines(const char* text, int16_t x, int16_t y, int16_t w, int16_t h);
static void     draw_status_lines(ExtCodec::State state, bool routeOk);
static void     draw_status_line(const char* text, int16_t& y);
static int16_t  sine_sample(uint32_t& phase);
static void     fill_sine_chunk(int16_t* samples, size_t sampleCount, uint32_t& phase);

} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

MasterTestView::MasterTestView() : FlyGuiView(FLYGUI_VIEW_MASTER_TEST) {}

void MasterTestView::onLoad()
{
    outputMode_ = OutputMode::None;
    micMode_    = MicMode::None;
    routeOk_    = applyAudioRouting();
    lastExtCodecGeneration_ = ExtCodec::stateGeneration();
    resetMicHistory();
    lastRefreshMs_ = 0;
    setDirty();
    FlyGuiView::onLoad();
}

void MasterTestView::onUnload()
{
    resetAudioRouting();
    FlyGuiView::onUnload();
}

bool MasterTestView::handleTouch(const FlyGuiTouchEvent& event)
{
    const Cell cell = cell_at(event.x, event.y);
    if (cell == Cell::None)
    {
        return false;
    }

    if (!event.justReleased)
    {
        return true;
    }

    haptic_play_click();
    switch (cell)
    {
    case Cell::EarbudOutput:
        toggleOutput(OutputMode::Earbud);
        break;
    case Cell::InternalSpeaker:
        toggleOutput(OutputMode::InternalSpeaker);
        break;
    case Cell::EarbudMic:
        toggleMic(MicMode::EarbudMic);
        break;
    case Cell::InternalMic:
        toggleMic(MicMode::InternalLineInMic);
        break;
    case Cell::Status:
    case Cell::MicHistory:
    case Cell::None:
    default:
        break;
    }

    return true;
}

bool MasterTestView::redraw(bool forced)
{
    const uint32_t extCodecGeneration = ExtCodec::stateGeneration();
    if (extCodecGeneration != lastExtCodecGeneration_)
    {
        lastExtCodecGeneration_ = extCodecGeneration;
        routeOk_                = applyAudioRouting();
        setDirty();
    }

    queueSineAudio();

    const uint32_t now = millis();
    if (!forced && !dirty() && static_cast<uint32_t>(now - lastRefreshMs_) < kRefreshPeriodMs)
    {
        return false;
    }

    const bool fullRedraw = forced || dirty();
    if (fullRedraw)
    {
        drawFrame();
        drawButtons();
    }

    drawStatusCell();
    updateMicHistory();
    drawMicHistory();

    lastRefreshMs_ = now;
    markClean();
    return true;
}

// -----------------------------------------------------------------------------
// Feature Logic
// -----------------------------------------------------------------------------

void MasterTestView::toggleOutput(OutputMode mode)
{
    outputMode_ = outputMode_ == mode ? OutputMode::None : mode;
    routeOk_    = applyAudioRouting();
    setDirty();
}

void MasterTestView::toggleMic(MicMode mode)
{
    micMode_ = micMode_ == mode ? MicMode::None : mode;
    routeOk_ = applyAudioRouting();
    setDirty();
}

bool MasterTestView::applyAudioRouting()
{
    AudioManager::forceExternalHeadphoneForExternalCodec(outputMode_ == OutputMode::Earbud);
    AudioManager::forceInternalSpeakerForExternalCodec(outputMode_ == OutputMode::InternalSpeaker);

    switch (micMode_)
    {
    case MicMode::EarbudMic:
        AudioManager::setExternalCodecMicOverride(AudioManager::ExternalCodecMicOverride::DedicatedMic);
        break;
    case MicMode::InternalLineInMic:
        AudioManager::setExternalCodecMicOverride(AudioManager::ExternalCodecMicOverride::LineInRight);
        break;
    case MicMode::None:
    default:
        AudioManager::setExternalCodecMicOverride(AudioManager::ExternalCodecMicOverride::Auto);
        break;
    }

    AudioManager::setVolume(AudioManager::kMaxVolume);
    AudioManager::setSpeakerMuted(false);
    AudioManager::setMicMuted(false);

    const bool outputActive = outputMode_ != OutputMode::None;
    const bool micActive    = micMode_ != MicMode::None;
    AudioManager::bluetoothToSpeakerFifo().clear();

    if (outputActive && micActive)
    {
        return AudioManager::enableFullDuplexMode(AudioManager::kSampleRateHz);
    }
    if (outputActive)
    {
        return AudioManager::enableSpeakerMode(AudioManager::kSampleRateHz);
    }
    if (micActive)
    {
        return AudioManager::enableMicMode();
    }

    AudioManager::stop();
    return true;
}

void MasterTestView::resetAudioRouting()
{
    AudioManager::stop();
    AudioManager::forceExternalHeadphoneForExternalCodec(false);
    AudioManager::forceInternalSpeakerForExternalCodec(false);
    AudioManager::setExternalCodecMicOverride(AudioManager::ExternalCodecMicOverride::Auto);
    AudioManager::bluetoothToSpeakerFifo().clear();
}

void MasterTestView::queueSineAudio()
{
    if (outputMode_ == OutputMode::None || !routeOk_)
    {
        return;
    }

    AudioFifo& fifo  = AudioManager::bluetoothToSpeakerFifo();
    size_t     chunks = 0;
    while (fifo.usedSamples() < kSineTargetSamples && fifo.availableToWrite() > 0 && chunks < kSineMaxChunks)
    {
        int16_t samples[kSineChunkSamples] = {};
        const size_t chunk = min(kSineChunkSamples, fifo.availableToWrite());
        fill_sine_chunk(samples, chunk, sinePhase_);
        fifo.queue(samples, chunk, AudioManager::kSampleRateHz);
        ++chunks;
    }
}

void MasterTestView::resetMicHistory()
{
    memset(micHistory_, 0, sizeof(micHistory_));
}

void MasterTestView::updateMicHistory()
{
    memmove(micHistory_, micHistory_ + 1, sizeof(micHistory_) - 1);
    micHistory_[sizeof(micHistory_) - 1] = micMode_ == MicMode::None ? 0 : AudioManager::micScaledPeakLevel();
}

// -----------------------------------------------------------------------------
// Drawing
// -----------------------------------------------------------------------------

void MasterTestView::drawFrame()
{
    thefly_display.fillRect(0,
                            FlyGui::kTopBarHeight,
                            thefly_display.width(),
                            thefly_display.height() - FlyGui::kTopBarHeight,
                            TFT_BLACK);

    for (uint8_t row = 0; row < 2; ++row)
    {
        for (uint8_t col = 0; col < 3; ++col)
        {
            draw_cell_base(col, row);
        }
    }
}

void MasterTestView::drawStatusCell()
{
    draw_cell_base(0, 0);
    draw_status_lines(ExtCodec::state(), routeOk_);
}

void MasterTestView::drawMicHistory()
{
    draw_cell_base(0, 1);

    const int16_t x      = cell_x(0);
    const int16_t y      = cell_y(1);
    const int16_t w      = cell_w(0);
    const int16_t h      = cell_h(1);
    const int16_t graphX = x + static_cast<int16_t>((w - static_cast<int16_t>(sizeof(micHistory_))) / 2);
    const int16_t bottom = y + h - 4;
    const int16_t maxH   = h - 10;

    for (size_t i = 0; i < sizeof(micHistory_); ++i)
    {
        const int16_t barH = static_cast<int16_t>((static_cast<uint16_t>(micHistory_[i]) * maxH) / 100U);
        if (barH > 0)
        {
            thefly_display.drawFastVLine(graphX + static_cast<int16_t>(i), bottom - barH, barH, kBarColor);
        }
    }
}

void MasterTestView::drawButtons()
{
    draw_toggle_cell(1, 0, "earbud", outputMode_ == OutputMode::Earbud);
    draw_toggle_cell(2, 0, "speaker", outputMode_ == OutputMode::InternalSpeaker);
    draw_toggle_cell(1, 1, "earbud\nmic", micMode_ == MicMode::EarbudMic);
    draw_toggle_cell(2, 1, "internal\nmic", micMode_ == MicMode::InternalLineInMic);
}

namespace
{

// -----------------------------------------------------------------------------
// Drawing Helpers
// -----------------------------------------------------------------------------

Cell cell_at(int16_t x, int16_t y)
{
    if (x < 0 || y < FlyGui::kTopBarHeight || x >= thefly_display.width() || y >= thefly_display.height())
    {
        return Cell::None;
    }

    const uint8_t col = static_cast<uint8_t>((static_cast<int32_t>(x) * 3) / thefly_display.width());
    const uint8_t row = static_cast<uint8_t>(((static_cast<int32_t>(y) - FlyGui::kTopBarHeight) * 2) /
                                             (thefly_display.height() - FlyGui::kTopBarHeight));
    if (row == 0 && col == 0)
    {
        return Cell::Status;
    }
    if (row == 0 && col == 1)
    {
        return Cell::EarbudOutput;
    }
    if (row == 0 && col == 2)
    {
        return Cell::InternalSpeaker;
    }
    if (row == 1 && col == 0)
    {
        return Cell::MicHistory;
    }
    if (row == 1 && col == 1)
    {
        return Cell::EarbudMic;
    }
    if (row == 1 && col == 2)
    {
        return Cell::InternalMic;
    }

    return Cell::None;
}

int16_t cell_x(uint8_t col)
{
    return static_cast<int16_t>((static_cast<int32_t>(thefly_display.width()) * col) / 3);
}

int16_t cell_y(uint8_t row)
{
    return static_cast<int16_t>(FlyGui::kTopBarHeight +
                               ((static_cast<int32_t>(thefly_display.height() - FlyGui::kTopBarHeight) * row) / 2));
}

int16_t cell_w(uint8_t col)
{
    return static_cast<int16_t>(cell_x(col + 1) - cell_x(col));
}

int16_t cell_h(uint8_t row)
{
    return static_cast<int16_t>(cell_y(row + 1) - cell_y(row));
}

void draw_cell_base(uint8_t col, uint8_t row)
{
    const int16_t x = cell_x(col);
    const int16_t y = cell_y(row);
    const int16_t w = cell_w(col);
    const int16_t h = cell_h(row);

    thefly_display.fillRect(x, y, w, h, TFT_BLACK);
    thefly_display.drawRect(x, y, w, h, kGridColor);
}

void draw_toggle_cell(uint8_t col, uint8_t row, const char* label, bool active)
{
    draw_cell_base(col, row);

    const int16_t x = cell_x(col);
    const int16_t y = cell_y(row);
    const int16_t w = cell_w(col);
    const int16_t h = cell_h(row);
    if (active)
    {
        thefly_display.drawRect(x + kCellInset, y + kCellInset, w - (kCellInset * 2), h - (kCellInset * 2), kActiveColor);
        thefly_display.drawRect(
            x + kCellInset + 1, y + kCellInset + 1, w - (kCellInset * 2) - 2, h - (kCellInset * 2) - 2, kActiveColor);
    }

    draw_centered_lines(label, x, y, w, h);
}

void draw_centered_lines(const char* text, int16_t x, int16_t y, int16_t w, int16_t h)
{
    if (!text)
    {
        return;
    }

    uint8_t lineCount = 1;
    for (const char* p = text; *p; ++p)
    {
        if (*p == '\n')
        {
            ++lineCount;
        }
    }

    thefly_display.setTextFont(kButtonTextFont);
    thefly_display.setTextSize(1.0f);
    thefly_display.setTextDatum(top_center);
    thefly_display.setTextColor(TFT_WHITE, TFT_BLACK);

    const int16_t centerX = x + w / 2;
    int16_t       lineY   = y + static_cast<int16_t>((h - (lineCount * kButtonLineHeight)) / 2);
    const char*   start   = text;
    for (const char* p = text;; ++p)
    {
        if (*p != '\n' && *p != '\0')
        {
            continue;
        }

        char line[24] = {};
        const size_t len = min(static_cast<size_t>(p - start), sizeof(line) - 1);
        memcpy(line, start, len);
        thefly_display.drawString(line, centerX, lineY);
        lineY += kButtonLineHeight;

        if (*p == '\0')
        {
            break;
        }
        start = p + 1;
    }
}

void draw_status_lines(ExtCodec::State state, bool routeOk)
{
    int16_t y = cell_y(0) + 5;

    thefly_display.setTextFont(kStatusTextFont);
    thefly_display.setTextSize(1.0f);
    thefly_display.setTextDatum(top_left);
    thefly_display.setTextColor(TFT_WHITE, TFT_BLACK);

    draw_status_line("EXTCODEC", y);
    switch (state)
    {
    case ExtCodec::EXTCODEC_UNAVAIL:
        draw_status_line("UNAVAIL", y);
        break;
    case ExtCodec::EXTCODEC_NO_EARBUD:
        draw_status_line("NO_EARBUD", y);
        break;
    case ExtCodec::EXTCODEC_YES_EARBUD:
        draw_status_line("YES_EARBUD", y);
        break;
    case ExtCodec::EXTCODEC_YES_EARBUD_WITH_MIC:
        draw_status_line("YES_EARBUD", y);
        draw_status_line("WITH_MIC", y);
        break;
    case ExtCodec::EXTCODEC_YES_EARBUD_W_WEAK_MIC:
        draw_status_line("YES_EARBUD", y);
        draw_status_line("WEAK_MIC", y);
        break;
    default:
        draw_status_line("UNKNOWN", y);
        break;
    }

    char line[24] = {};
    snprintf(line, sizeof(line), "ear:%u", static_cast<unsigned>(ExtCodec::earbudSenseRaw()));
    draw_status_line(line, y);
    snprintf(line, sizeof(line), "mic:%u", static_cast<unsigned>(ExtCodec::inlineMicSenseRaw()));
    draw_status_line(line, y);
    snprintf(line, sizeof(line), "pll:%u route:%s", ExtCodec::pllLocked() ? 1U : 0U, routeOk ? "ok" : "fail");
    draw_status_line(line, y);
}

void draw_status_line(const char* text, int16_t& y)
{
    thefly_display.drawString(text ? text : "", cell_x(0) + 5, y);
    y += kStatusLineHeight;
}

// -----------------------------------------------------------------------------
// Audio Helpers
// -----------------------------------------------------------------------------

int16_t sine_sample(uint32_t& phase)
{
    const size_t index = (phase >> 16U) & (kSineTableSize - 1U);
    phase += kPhaseStep;
    return kSineTable[index];
}

void fill_sine_chunk(int16_t* samples, size_t sampleCount, uint32_t& phase)
{
    for (size_t i = 0; i < sampleCount; ++i)
    {
        samples[i] = sine_sample(phase);
    }
}

} // namespace
