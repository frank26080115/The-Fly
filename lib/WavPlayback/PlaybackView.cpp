#include "PlaybackView.h"

#include <algorithm>
#include <stdio.h>
#include <string.h>

#include "AudioManager.h"
#include "MicroSdCard.h"
#include "../FlyGui/FlyGuiText.h"
#include "../FlyGuiViews/ModalDialog.h"
#include "../FlyGuiViews/ScrollView/ScrollView.h"
#include "WavPlayback.h"
#include "esp_log.h"
#include "sprites.h"
#include "utilfuncs.h"

extern ModalDialog* get_modal_dialog();
extern ScrollView* get_scroll_view();

namespace
{
constexpr const char* TAG = "PlaybackView";

constexpr int16_t kIconX       = 4;
constexpr int16_t kIconY       = FlyGui::kTopBarHeight;
constexpr int16_t kIconSize    = 50;
constexpr int16_t kFileTextX   = 60;
constexpr int16_t kFileTextY   = FlyGui::kTopBarHeight + 7;
constexpr int16_t kFileTextMaxY = FlyGui::kTopBarHeight + kIconSize;
constexpr int16_t kFileTextRightMargin = 4;
constexpr int16_t kFileLineHeight = 15;
constexpr int16_t kScrubWidth  = 300;
constexpr int16_t kScrubHeight = 20;
constexpr int16_t kScrubY      = 64;
constexpr int16_t kDeleteConfirmTextY = kScrubY - 17;
constexpr int16_t kDeleteConfirmTextGap = 4;
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
constexpr uint32_t kDeleteConfirmDebounceMs = 1000;
constexpr const char* kDeleteConfirmText = "confirm delete?";
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

int16_t delete_x()
{
    return static_cast<int16_t>(thefly_display.width() - kButtonSize);
}

void copy_wrapped_prefix_that_fits(const char* text, char* out, size_t outSize, int16_t width, int16_t y, int16_t maxY, int16_t lineHeight)
{
    if (!out || outSize == 0)
    {
        return;
    }
    out[0] = '\0';

    if (!text || width <= 0 || lineHeight <= 0)
    {
        return;
    }

    char   candidate[kWrapLineMax] = {};
    size_t bestLen = 0;
    const size_t maxLen = std::min(strlen(text), std::min(outSize - 1, sizeof(candidate) - 1));
    for (size_t len = 1; len <= maxLen; ++len)
    {
        memcpy(candidate, text, len);
        candidate[len] = '\0';
        if (!wrapped_text_fits(candidate, width, y, maxY, lineHeight))
        {
            break;
        }
        bestLen = len;
    }

    if (bestLen > 0)
    {
        memcpy(out, text, bestLen);
    }
    out[bestLen] = '\0';
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
      deleteButton_(0, kIconY, kButtonSize, kButtonSize),
      playPauseButton_(0, kButtonY, kButtonSize, kButtonSize),
      volumeButton_(0, kButtonY, kButtonSize, kButtonSize),
      exitButton_(0, kButtonY, kButtonSize, kButtonSize),
      scrubBar_(0, kScrubY, kScrubWidth, kScrubHeight),
      precisionScrubBar_(0, kPrecisionScrubY, kScrubWidth, kScrubHeight)
{
    activeInstance_ = this;

    recordIcon_.setSprite(sprite_record_50, SPRITE_RECORD_50_WIDTH, SPRITE_RECORD_50_HEIGHT, SPRITE_RECORD_50_BYTES);
    addItem(recordIcon_);

    deleteButton_.setSprite(sprite_trash_50, SPRITE_TRASH_50_WIDTH, SPRITE_TRASH_50_HEIGHT, SPRITE_TRASH_50_BYTES);
    deleteButton_.setFaded(true);
    deleteButton_.setCallback(deleteThunk);
    deleteButton_.setVisible(false);
    deleteButton_.setTouchable(false);
    addItem(deleteButton_);

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
    setDeleteButtonVisible(false);
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
    setDeleteButtonVisible(false);
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

void PlaybackView::deleteThunk(uint32_t pressDurationMs)
{
    (void)pressDurationMs;
    if (activeInstance_)
    {
        activeInstance_->handleDelete();
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

void PlaybackView::handleDelete()
{
    if (!deleteButton_.visible() || !WavPlayback::paused())
    {
        return;
    }

    if (!deleteArmed_)
    {
        setDeleteArmed(true);
        return;
    }

    if (static_cast<uint32_t>(millis() - deleteArmedAtMs_) < kDeleteConfirmDebounceMs)
    {
        return;
    }

    char deletedName[sizeof(fileName_)] = {};
    strlcpy(deletedName, fileName_[0] != '\0' ? fileName_ : "Audio file", sizeof(deletedName));

    WavPlayback::stop();
    if (gui())
    {
        gui()->setAudioActive(false);
    }

    bool deleted = false;
    const char* detail = nullptr;
    if (path_[0] == '\0')
    {
        detail = "No file selected";
    }
    else if (!MicroSdCard::isReady())
    {
        detail = "microSD not ready";
    }
    else
    {
        FsFile file;
        if (!file.open(path_, O_RDONLY))
        {
            detail = "File not found";
        }
        else if (file.isDir())
        {
            file.close();
            detail = "Target is a folder";
        }
        else
        {
            file.close();
            deleted = MicroSdCard::fs().remove(path_);
            detail = deleted ? deletedName : "File delete failed";
        }
    }

    if (deleted)
    {
        ESP_LOGI(TAG, "deleted playback file: %s", path_);
        if (ScrollView* scrollView = get_scroll_view())
        {
            scrollView->populateFiles();
        }
    }
    else
    {
        ESP_LOGW(TAG, "failed to delete playback file: %s (%s)", path_, detail ? detail : "unknown");
    }

    showDeleteResultDialog(deleted, detail);
}

void PlaybackView::handleScrub(uint32_t positionMs)
{
    setDeleteArmed(false);
    WavPlayback::setPositionMs(positionMs);
    const uint32_t current = WavPlayback::positionMs();
    scrubBar_.setPositionMs(current);
    precisionScrubBar_.setPositionMs(current);
    precisionWindowInitialized_ = false;
    setDirty();
}

void PlaybackView::handlePrecisionScrub(uint32_t positionMs)
{
    setDeleteArmed(false);
    WavPlayback::setPositionMs(positionMs);
    const uint32_t current = WavPlayback::positionMs();
    scrubBar_.setPositionMs(current);
    precisionScrubBar_.setPositionMs(current);
    setDirty();
}

void PlaybackView::startPlayback()
{
    setDeleteButtonVisible(false);
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
    deleteButton_.relocate(delete_x(), kIconY, kButtonSize, kButtonSize);
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
    syncDeleteButton(shouldShowPause);
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

void PlaybackView::syncDeleteButton(bool playing)
{
    setDeleteButtonVisible(!playing && WavPlayback::active());
}

void PlaybackView::setVolumeIndex(uint8_t index)
{
    const uint8_t count = static_cast<uint8_t>(sizeof(kVolumeOptions) / sizeof(kVolumeOptions[0]));
    volumeIndex_ = count == 0 ? 0 : static_cast<uint8_t>(index % count);

    const VolumeOption& option = kVolumeOptions[volumeIndex_];
    volumeButton_.setSprite(option.sprite, option.width, option.height, option.bytes);
    WavPlayback::setVolume(option.volume);
}

void PlaybackView::setDeleteButtonVisible(bool visible)
{
    if (!visible)
    {
        setDeleteArmed(false);
    }

    if (deleteButton_.visible() == visible)
    {
        return;
    }

    if (visible)
    {
        setDeleteArmed(false);
    }

    deleteButton_.setVisible(visible);
    deleteButton_.setTouchable(visible);
    frameDirty_ = true;
    setDirty();
}

void PlaybackView::setDeleteArmed(bool armed)
{
    if (deleteArmed_ == armed)
    {
        return;
    }

    deleteArmed_ = armed;
    deleteArmedAtMs_ = armed ? millis() : 0;
    if (armed)
    {
        deleteButton_.setSprite(sprite_trashconfirm_50,
                                SPRITE_TRASHCONFIRM_50_WIDTH,
                                SPRITE_TRASHCONFIRM_50_HEIGHT,
                                SPRITE_TRASHCONFIRM_50_BYTES);
        deleteButton_.setFaded(false);
    }
    else
    {
        deleteButton_.setSprite(sprite_trash_50, SPRITE_TRASH_50_WIDTH, SPRITE_TRASH_50_HEIGHT, SPRITE_TRASH_50_BYTES);
        deleteButton_.setFaded(true);
    }
    frameDirty_ = true;
    setDirty();
}

void PlaybackView::showDeleteResultDialog(bool deleted, const char* detail)
{
    ModalDialog* dialog = get_modal_dialog();
    FlyGui*      owner  = gui();
    if (!dialog || !owner)
    {
        if (owner)
        {
            owner->showView(FLYGUI_VIEW_SCROLL);
        }
        return;
    }

    char message[160] = {};
    if (deleted)
    {
        snprintf(message, sizeof(message), "Deleted\n%s", detail && detail[0] != '\0' ? detail : "audio file");
        dialog->configure(sprite_thumbsup_100,
                          SPRITE_THUMBSUP_100_BYTES,
                          SPRITE_THUMBSUP_100_WIDTH,
                          SPRITE_THUMBSUP_100_HEIGHT,
                          message,
                          FLYGUI_VIEW_SCROLL);
    }
    else
    {
        snprintf(message, sizeof(message), "Delete failed\n%s", detail && detail[0] != '\0' ? detail : "unknown error");
        dialog->configure(sprite_warning_100,
                          SPRITE_WARNING_100_BYTES,
                          SPRITE_WARNING_100_WIDTH,
                          SPRITE_WARNING_100_HEIGHT,
                          message,
                          FLYGUI_VIEW_SCROLL);
    }

    owner->showView(FLYGUI_VIEW_MODAL_DIALOG);
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
    drawDeleteConfirmText();

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
    const int16_t fontFitWidth = static_cast<int16_t>(thefly_display.width() - kFileTextX - kFileTextRightMargin);

    thefly_display.setTextDatum(top_left);
    thefly_display.setTextSize(kTextSize);
    thefly_display.setTextColor(TFT_WHITE, TFT_BLACK);

    thefly_display.setTextFont(kLargeFileFont);
    const int16_t largeLineHeight = static_cast<int16_t>(thefly_display.fontHeight() + kLargeFileLineGap);
    const bool useLargeFont = wrapped_text_fits(text, fontFitWidth, kFileTextY, kFileTextMaxY, largeLineHeight);

    thefly_display.setTextFont(useLargeFont ? kLargeFileFont : kNormalFont);
    const int16_t lineHeight = useLargeFont ? largeLineHeight : kFileLineHeight;
    int16_t drawWidth = fontFitWidth;
    char clippedText[sizeof(fileName_)] = {};
    if (deleteButton_.visible())
    {
        drawWidth = static_cast<int16_t>(deleteButton_.x() - kFileTextX - kFileTextRightMargin);
        copy_wrapped_prefix_that_fits(text, clippedText, sizeof(clippedText), drawWidth, kFileTextY, kFileTextMaxY, lineHeight);
        text = clippedText;
    }

    FlyGuiTextUtil::drawWrappedText(text,
                                    kFileTextX,
                                    kFileTextY,
                                    drawWidth,
                                    kFileTextMaxY,
                                    lineHeight);
}

void PlaybackView::drawDeleteConfirmText() const
{
    if (!deleteArmed_ || !deleteButton_.visible())
    {
        return;
    }

    const int16_t textRight = static_cast<int16_t>(deleteButton_.x() - kDeleteConfirmTextGap);
    thefly_display.setTextDatum(top_right);
    thefly_display.setTextFont(kNormalFont);
    thefly_display.setTextSize(kTextSize);
    thefly_display.setTextColor(TFT_WHITE, TFT_BLACK);
    const int16_t textWidth = thefly_display.textWidth(kDeleteConfirmText);
    const int16_t textHeight = thefly_display.fontHeight();
    const int16_t clearX = static_cast<int16_t>(std::max<int16_t>(0, textRight - textWidth - 2));
    thefly_display.fillRect(clearX,
                            kDeleteConfirmTextY,
                            static_cast<int16_t>(textRight - clearX + 2),
                            static_cast<int16_t>(textHeight + 1),
                            TFT_BLACK);
    thefly_display.drawString(kDeleteConfirmText, textRight, kDeleteConfirmTextY);
}
