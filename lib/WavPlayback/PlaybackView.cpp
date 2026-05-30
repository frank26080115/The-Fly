#include "PlaybackView.h"

#include <algorithm>
#include <stdio.h>
#include <string.h>

#include "AudioManager.h"
#include "../FlyGui/FlyGuiText.h"
#include "WavPlayback.h"
#include "esp_log.h"
#include "sprites.h"
#include "utilfuncs.h"

namespace
{
constexpr const char* TAG = "PlaybackView";

constexpr int16_t kIconX       = 4;
constexpr int16_t kIconY       = FlyGui::kTopBarHeight;
constexpr int16_t kIconSize    = 50;
constexpr int16_t kFileTextX   = 60;
constexpr int16_t kFileTextY   = FlyGui::kTopBarHeight + 7;
constexpr int16_t kFileTextMaxY = FlyGui::kTopBarHeight + kIconSize;
constexpr int16_t kFileLineHeight = 15;
constexpr int16_t kScrubWidth  = 300;
constexpr int16_t kScrubHeight = 20;
constexpr int16_t kScrubY      = 64;
constexpr int16_t kPrecisionScrubY = 124;
constexpr int16_t kButtonY     = 180;
constexpr int16_t kButtonSize  = 50;
constexpr uint8_t kNormalFont  = 2;
constexpr uint8_t kLargeFileFont = 4;
constexpr int16_t kLargeFileLineGap = 2;
constexpr float   kTextSize    = 1.0f;
constexpr uint32_t kLongFilePrecisionThresholdMs = 120000;
constexpr uint32_t kPrecisionWindowMs = 60000;
constexpr uint32_t kPrecisionHalfWindowMs = kPrecisionWindowMs / 2;
constexpr uint32_t kPrecisionCatchupMs = 1000;
constexpr uint16_t kPrecisionScrubColor = 0xFDE0; // #FCBC00 converted to RGB565. 0xFF20 might also work

constexpr size_t kWrapLineMax = 128;
constexpr size_t kNoSpace = static_cast<size_t>(-1);

void trim_leading_spaces(char* line, size_t& len)
{
    size_t first = 0;
    while (first < len && line[first] == ' ')
    {
        ++first;
    }

    if (first == 0)
    {
        return;
    }

    memmove(line, line + first, len - first);
    len -= first;
    line[len] = '\0';
}

void recalc_last_space(const char* line, size_t len, size_t& lastSpace)
{
    lastSpace = kNoSpace;
    for (size_t i = 0; i < len; ++i)
    {
        if (line[i] == ' ')
        {
            lastSpace = i;
        }
    }
}

bool count_wrapped_line(char* line, size_t& len, int16_t& y, int16_t maxY, int16_t lineHeight)
{
    trim_leading_spaces(line, len);
    while (len > 0 && line[len - 1] == ' ')
    {
        line[--len] = '\0';
    }

    if (len == 0)
    {
        return true;
    }

    if (y + lineHeight > maxY)
    {
        return false;
    }

    y += lineHeight;
    len = 0;
    line[0] = '\0';
    return true;
}

bool wrapped_text_fits(const char* text, int16_t width, int16_t y, int16_t maxY, int16_t lineHeight)
{
    if (!text || width <= 0 || lineHeight <= 0)
    {
        return false;
    }

    char line[kWrapLineMax] = {};
    size_t len = 0;
    size_t lastSpace = kNoSpace;
    bool canFit = true;

    for (const char* p = text; *p && canFit; ++p)
    {
        const char c = *p;
        if (c == '\r')
        {
            continue;
        }

        if (c == '\n')
        {
            canFit = count_wrapped_line(line, len, y, maxY, lineHeight);
            lastSpace = kNoSpace;
            continue;
        }

        if (len + 1 >= sizeof(line))
        {
            canFit = count_wrapped_line(line, len, y, maxY, lineHeight);
            lastSpace = kNoSpace;
            if (!canFit)
            {
                break;
            }
        }

        line[len++] = c;
        line[len] = '\0';
        if (c == ' ')
        {
            lastSpace = len - 1;
        }

        while (thefly_display.textWidth(line) > width && len > 0 && canFit)
        {
            if (lastSpace != kNoSpace && lastSpace > 0)
            {
                char remainder[kWrapLineMax] = {};
                strncpy(remainder, line + lastSpace + 1, sizeof(remainder) - 1);

                line[lastSpace] = '\0';
                size_t lineLen = lastSpace;
                canFit = count_wrapped_line(line, lineLen, y, maxY, lineHeight);

                strncpy(line, remainder, sizeof(line) - 1);
                line[sizeof(line) - 1] = '\0';
                len = strlen(line);
                recalc_last_space(line, len, lastSpace);
            }
            else if (len > 1)
            {
                const char overflow = line[len - 1];
                line[len - 1] = '\0';
                size_t lineLen = len - 1;
                canFit = count_wrapped_line(line, lineLen, y, maxY, lineHeight);

                line[0] = overflow;
                line[1] = '\0';
                len = 1;
                lastSpace = overflow == ' ' ? 0 : kNoSpace;
            }
            else
            {
                canFit = count_wrapped_line(line, len, y, maxY, lineHeight);
                lastSpace = kNoSpace;
            }
        }
    }

    if (canFit && len > 0)
    {
        canFit = count_wrapped_line(line, len, y, maxY, lineHeight);
    }

    return canFit;
}

int16_t scrub_x()
{
    return static_cast<int16_t>((thefly_display.width() - kScrubWidth) / 2);
}

int16_t button_x(uint8_t column)
{
    const int16_t columnWidth = static_cast<int16_t>(thefly_display.width() / 3);
    return static_cast<int16_t>((columnWidth * column) + (columnWidth / 2) - (kButtonSize / 2));
}

uint32_t precision_window_start(uint32_t currentMs, uint32_t totalMs)
{
    if (totalMs <= kPrecisionWindowMs)
    {
        return 0;
    }

    if (currentMs <= kPrecisionHalfWindowMs)
    {
        return 0;
    }

    const uint32_t latestStart = totalMs - kPrecisionWindowMs;
    const uint32_t centeredStart = currentMs - kPrecisionHalfWindowMs;
    return centeredStart > latestStart ? latestStart : centeredStart;
}

uint32_t step_toward(uint32_t value, uint32_t target, uint32_t step)
{
    if (value < target)
    {
        const uint32_t delta = target - value;
        return value + (delta < step ? delta : step);
    }
    if (value > target)
    {
        const uint32_t delta = value - target;
        return value - (delta < step ? delta : step);
    }

    return value;
}

struct VolumeOption
{
    uint8_t        volume;
    const uint8_t* sprite;
    uint32_t       width;
    uint32_t       height;
    size_t         bytes;
};

const VolumeOption kVolumeOptions[] = {
    { AudioManager::kMaxVolume, sprite_speaker_50, SPRITE_SPEAKER_50_WIDTH, SPRITE_SPEAKER_50_HEIGHT, SPRITE_SPEAKER_50_BYTES },
    { 20, sprite_speaker_66_50, SPRITE_SPEAKER_66_50_WIDTH, SPRITE_SPEAKER_66_50_HEIGHT, SPRITE_SPEAKER_66_50_BYTES },
    { 10, sprite_speaker_33_50, SPRITE_SPEAKER_33_50_WIDTH, SPRITE_SPEAKER_33_50_HEIGHT, SPRITE_SPEAKER_33_50_BYTES },
    { AudioManager::kMinVolume, sprite_speaker_00_50, SPRITE_SPEAKER_00_50_WIDTH, SPRITE_SPEAKER_00_50_HEIGHT, SPRITE_SPEAKER_00_50_BYTES },
};
} // namespace

PlaybackView* PlaybackView::activeInstance_ = nullptr;

PlaybackView::PlaybackView()
    : FlyGuiView(FLYGUI_VIEW_PLAYBACK),
      recordIcon_(kIconX, kIconY, kIconSize, kIconSize),
      playPauseButton_(0, kButtonY, kButtonSize, kButtonSize),
      volumeButton_(0, kButtonY, kButtonSize, kButtonSize),
      exitButton_(0, kButtonY, kButtonSize, kButtonSize),
      scrubBar_(0, kScrubY, kScrubWidth, kScrubHeight),
      precisionScrubBar_(0, kPrecisionScrubY, kScrubWidth, kScrubHeight)
{
    activeInstance_ = this;

    recordIcon_.setSprite(sprite_record_50, SPRITE_RECORD_50_WIDTH, SPRITE_RECORD_50_HEIGHT, SPRITE_RECORD_50_BYTES);
    addItem(recordIcon_);

    playPauseButton_.setSprite(sprite_playback_play_50,
                               SPRITE_PLAYBACK_PLAY_50_WIDTH,
                               SPRITE_PLAYBACK_PLAY_50_HEIGHT,
                               SPRITE_PLAYBACK_PLAY_50_BYTES);
    playPauseButton_.setCallback(playPauseThunk);
    addItem(playPauseButton_);

    setVolumeIndex(0);
    volumeButton_.setCallback(volumeThunk);
    addItem(volumeButton_);

    exitButton_.setSprite(sprite_squarex_50, SPRITE_SQUAREX_50_WIDTH, SPRITE_SQUAREX_50_HEIGHT, SPRITE_SQUAREX_50_BYTES);
    exitButton_.setCallback(exitThunk);
    addItem(exitButton_);

    scrubBar_.setColors(TFT_BLUE, TFT_BLACK, TFT_WHITE);
    scrubBar_.setHitboxYOffsets(-10, 30);
    scrubBar_.setShowText(true);
    scrubBar_.setScrubCallback(scrubThunk, this);
    addItem(scrubBar_);

    precisionScrubBar_.setColors(kPrecisionScrubColor, TFT_BLACK, TFT_WHITE);
    precisionScrubBar_.setHitboxYOffsets(0, 40);
    precisionScrubBar_.setShowText(false);
    precisionScrubBar_.setScrubCallback(precisionScrubThunk, this);
    precisionScrubBar_.setVisible(false);
    precisionScrubBar_.setTouchable(false);
    addItem(precisionScrubBar_);
}

void PlaybackView::configureFile(const char* path)
{
    strlcpy(path_, path ? path : "", sizeof(path_));
    basename_for_path_no_ext(path_, fileName_, sizeof(fileName_));
    statusText_[0] = '\0';
    frameDirty_ = true;
    setDirty();
}

void PlaybackView::onLoad()
{
    activeInstance_ = this;
    layoutItems();
    startPlayback();
    if (gui())
    {
        gui()->setAudioActive(true);
    }
    FlyGuiView::onLoad();
}

void PlaybackView::onUnload()
{
    WavPlayback::stop();
    if (gui())
    {
        gui()->setAudioActive(false);
    }
    FlyGuiView::onUnload();
}

bool PlaybackView::handleTouch(const FlyGuiTouchEvent& event)
{
    if ((event.justPressed || event.pressed || event.justReleased) &&
        event.y >= kButtonY &&
        event.y < static_cast<int16_t>(kButtonY + kButtonSize))
    {
        const int16_t screenWidth = thefly_display.width();
        if (screenWidth > 0)
        {
            FlyGuiItem* button = nullptr;
            if (event.x < screenWidth / 3)
            {
                button = &playPauseButton_;
            }
            else if (event.x < (screenWidth * 2) / 3)
            {
                button = &volumeButton_;
            }
            else
            {
                button = &exitButton_;
            }

            if (button)
            {
                FlyGuiTouchEvent routed = event;
                routed.x = static_cast<int16_t>(button->x() + button->width() / 2);
                routed.y = static_cast<int16_t>(button->y() + button->height() / 2);
                return button->handleTouch(routed);
            }
        }
    }

    return FlyGuiView::handleTouch(event);
}

void PlaybackView::redraw(bool forced)
{
    layoutItems();
    syncControls();

    const bool repaintFrame = forced || frameDirty_;
    drawFrame(repaintFrame);
    FlyGuiView::redraw(repaintFrame);

    if (repaintFrame)
    {
        frameDirty_ = false;
        markClean();
    }
}

void PlaybackView::onPressLeft()
{
    playPauseButton_.trigger();
}

void PlaybackView::onPressMid()
{
    volumeButton_.trigger();
}

void PlaybackView::onPressRight()
{
    exitButton_.trigger();
}

void PlaybackView::playPauseThunk(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    if (activeInstance_)
    {
        activeInstance_->handlePlayPause();
    }
}

void PlaybackView::volumeThunk(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    if (activeInstance_)
    {
        activeInstance_->handleVolume();
    }
}

void PlaybackView::exitThunk(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    if (activeInstance_)
    {
        activeInstance_->handleExit();
    }
}

void PlaybackView::scrubThunk(uint32_t positionMs, void* context)
{
    if (context)
    {
        static_cast<PlaybackView*>(context)->handleScrub(positionMs);
    }
}

void PlaybackView::precisionScrubThunk(uint32_t positionMs, void* context)
{
    if (context)
    {
        static_cast<PlaybackView*>(context)->handlePrecisionScrub(positionMs);
    }
}

void PlaybackView::handlePlayPause()
{
    WavPlayback::togglePlaying();
    syncControls();
}

void PlaybackView::handleVolume()
{
    setVolumeIndex(static_cast<uint8_t>((volumeIndex_ + 1) % (sizeof(kVolumeOptions) / sizeof(kVolumeOptions[0]))));
}

void PlaybackView::handleExit()
{
    WavPlayback::stop();
    FlyGui::quickScreenFade();
    if (gui())
    {
        gui()->showView(FLYGUI_VIEW_SCROLL);
    }
}

void PlaybackView::handleScrub(uint32_t positionMs)
{
    WavPlayback::setPositionMs(positionMs);
    const uint32_t current = WavPlayback::positionMs();
    scrubBar_.setPositionMs(current);
    precisionScrubBar_.setPositionMs(current);
    precisionWindowInitialized_ = false;
    setDirty();
}

void PlaybackView::handlePrecisionScrub(uint32_t positionMs)
{
    WavPlayback::setPositionMs(positionMs);
    const uint32_t current = WavPlayback::positionMs();
    scrubBar_.setPositionMs(current);
    precisionScrubBar_.setPositionMs(current);
    setDirty();
}

void PlaybackView::startPlayback()
{
    scrubBar_.setPositionMs(0);
    scrubBar_.setTotalMs(0);
    precisionScrubBar_.setPositionMs(0);
    precisionScrubBar_.setRangeMs(0, 0);
    precisionScrubBar_.setVisible(false);
    precisionScrubBar_.setTouchable(false);
    precisionWindowInitialized_ = false;
    setVolumeIndex(volumeIndex_);

    if (!WavPlayback::start(path_))
    {
        snprintf(statusText_, sizeof(statusText_), "Playback failed: %s", WavPlayback::lastError());
        ESP_LOGW(TAG, "%s", statusText_);
    }

    scrubBar_.setTotalMs(WavPlayback::durationMs());
    scrubBar_.setPositionMs(WavPlayback::positionMs());
    syncControls();
    frameDirty_ = true;
}

void PlaybackView::layoutItems()
{
    recordIcon_.relocate(kIconX, kIconY, kIconSize, kIconSize);
    playPauseButton_.relocate(button_x(0), kButtonY, kButtonSize, kButtonSize);
    volumeButton_.relocate(button_x(1), kButtonY, kButtonSize, kButtonSize);
    exitButton_.relocate(button_x(2), kButtonY, kButtonSize, kButtonSize);
    scrubBar_.relocate(scrub_x(), kScrubY, kScrubWidth, kScrubHeight);
    precisionScrubBar_.relocate(scrub_x(), kPrecisionScrubY, kScrubWidth, kScrubHeight);
}

void PlaybackView::syncControls()
{
    const bool shouldShowPause = WavPlayback::playing();
    if (shouldShowPause != playIconPause_)
    {
        playIconPause_ = shouldShowPause;
        if (playIconPause_)
        {
            playPauseButton_.setSprite(sprite_playback_pause_50,
                                       SPRITE_PLAYBACK_PAUSE_50_WIDTH,
                                       SPRITE_PLAYBACK_PAUSE_50_HEIGHT,
                                       SPRITE_PLAYBACK_PAUSE_50_BYTES);
        }
        else
        {
            playPauseButton_.setSprite(sprite_playback_play_50,
                                       SPRITE_PLAYBACK_PLAY_50_WIDTH,
                                       SPRITE_PLAYBACK_PLAY_50_HEIGHT,
                                       SPRITE_PLAYBACK_PLAY_50_BYTES);
        }
    }

    const uint32_t total   = WavPlayback::durationMs();
    const uint32_t current = std::min(WavPlayback::positionMs(), total);
    scrubBar_.setTotalMs(total);
    scrubBar_.setPositionMs(current);
    syncPrecisionScrubBar(current, total, shouldShowPause);
}

void PlaybackView::syncPrecisionScrubBar(uint32_t currentMs, uint32_t totalMs, bool playing)
{
    const bool shouldShow = !playing && WavPlayback::active() && totalMs > kLongFilePrecisionThresholdMs;
    if (!shouldShow)
    {
        if (precisionScrubBar_.visible())
        {
            precisionScrubBar_.setVisible(false);
            precisionScrubBar_.setTouchable(false);
            frameDirty_ = true;
        }
        precisionWindowInitialized_ = false;
        return;
    }

    if (!precisionScrubBar_.visible())
    {
        precisionScrubBar_.setVisible(true);
        precisionScrubBar_.setTouchable(true);
        precisionWindowInitialized_ = false;
        frameDirty_ = true;
    }

    const uint32_t targetStart = precision_window_start(currentMs, totalMs);
    uint32_t start = targetStart;
    if (precisionWindowInitialized_)
    {
        start = step_toward(precisionScrubBar_.startMs(), targetStart, kPrecisionCatchupMs);
    }
    else
    {
        precisionWindowInitialized_ = true;
    }

    uint32_t end = start + kPrecisionWindowMs;
    if (end > totalMs)
    {
        end = totalMs;
        start = end > kPrecisionWindowMs ? end - kPrecisionWindowMs : 0;
    }

    precisionScrubBar_.setRangeMs(start, end);
    precisionScrubBar_.setPositionMs(currentMs);
}

void PlaybackView::setVolumeIndex(uint8_t index)
{
    const uint8_t count = static_cast<uint8_t>(sizeof(kVolumeOptions) / sizeof(kVolumeOptions[0]));
    volumeIndex_ = count == 0 ? 0 : static_cast<uint8_t>(index % count);

    const VolumeOption& option = kVolumeOptions[volumeIndex_];
    volumeButton_.setSprite(option.sprite, option.width, option.height, option.bytes);
    WavPlayback::setVolume(option.volume);
}

void PlaybackView::drawFrame(bool forced)
{
    if (!forced)
    {
        return;
    }

    const int16_t screenW = thefly_display.width();
    const int16_t screenH = thefly_display.height();
    thefly_display.fillRect(0,
                            FlyGui::kTopBarHeight,
                            screenW,
                            static_cast<int16_t>(screenH - FlyGui::kTopBarHeight),
                            TFT_BLACK);
    drawFileName();

    if (statusText_[0] != '\0')
    {
        thefly_display.setTextDatum(top_center);
        thefly_display.setTextFont(kNormalFont);
        thefly_display.setTextSize(kTextSize);
        thefly_display.setTextColor(TFT_RED, TFT_BLACK);
        thefly_display.drawString(statusText_, screenW / 2, 112);
    }
}

void PlaybackView::drawFileName() const
{
    const char* text = fileName_[0] != '\0' ? fileName_ : "No file";
    const int16_t textWidth = static_cast<int16_t>(thefly_display.width() - kFileTextX - 4);

    thefly_display.setTextDatum(top_left);
    thefly_display.setTextSize(kTextSize);
    thefly_display.setTextColor(TFT_WHITE, TFT_BLACK);

    thefly_display.setTextFont(kLargeFileFont);
    const int16_t largeLineHeight = static_cast<int16_t>(thefly_display.fontHeight() + kLargeFileLineGap);
    const bool useLargeFont = wrapped_text_fits(text, textWidth, kFileTextY, kFileTextMaxY, largeLineHeight);

    thefly_display.setTextFont(useLargeFont ? kLargeFileFont : kNormalFont);
    FlyGuiTextUtil::drawWrappedText(text,
                                    kFileTextX,
                                    kFileTextY,
                                    textWidth,
                                    kFileTextMaxY,
                                    useLargeFont ? largeLineHeight : kFileLineHeight);
}
