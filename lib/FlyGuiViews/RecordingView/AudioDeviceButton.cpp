#include "AudioDeviceButton.h"

#include "AudioManager.h"
#include "SpriteDraw.h"
#include "sprites.h"

namespace
{
constexpr int16_t kButtonSize = 100;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static void     draw_muted_overlay(const FlyGuiItem& item);
static uint16_t meter_color(uint8_t level);
} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

AudioDeviceButton::AudioDeviceButton(Device device, int16_t x, int16_t y)
    : FlyGuiItem(x, y, kButtonSize, kButtonSize), device_(device)
{
    if (device_ == Device::Mic)
    {
        setSprite(sprite_mic_100, SPRITE_MIC_100_WIDTH, SPRITE_MIC_100_HEIGHT, SPRITE_MIC_100_BYTES);
    }
    else
    {
        setSprite(sprite_speaker_100, SPRITE_SPEAKER_100_WIDTH, SPRITE_SPEAKER_100_HEIGHT, SPRITE_SPEAKER_100_BYTES);
    }
}

void AudioDeviceButton::setMicMode(bool micMode)
{
    const bool nextFaded = device_ == Device::Mic ? !micMode : micMode;
    if (faded() != nextFaded)
    {
        setFaded(nextFaded);
    }

    if (device_ == Device::Mic)
    {
        setMutedOverlay(!micMode);
    }
}

void AudioDeviceButton::setMutedOverlay(bool muted)
{
    if (mutedOverlay_ == muted)
    {
        return;
    }

    mutedOverlay_ = muted;
    setDirty();
    if (owner())
    {
        owner()->setDirty();
    }
}

bool AudioDeviceButton::drawAudioMeter()
{
    if (!visible())
    {
        return false;
    }

    const uint8_t level =
        device_ == Device::Mic ? AudioManager::micScaledPeakLevel() : AudioManager::speakerPeakLevel();
    const int16_t center = x() + width() / 2;
    const int16_t half   = static_cast<int16_t>((static_cast<uint32_t>(width() / 2) * level) / 100U);

    thefly_display.drawFastHLine(x(), y(), width(), TFT_BLACK);
    if (half <= 0)
    {
        return true;
    }

    thefly_display.drawFastHLine(center - half, y(), half * 2, meter_color(level));
    return true;
}

bool AudioDeviceButton::redraw(bool forced)
{
    const bool shouldDrawOverlay = visible() && (forced || dirty());
    bool       drawn             = FlyGuiItem::redraw(forced);
    if (shouldDrawOverlay && mutedOverlay_)
    {
        draw_muted_overlay(*this);
        drawn = true;
    }
    if (shouldDrawOverlay)
    {
        drawn |= drawAudioMeter();
    }
    return drawn;
}

namespace
{

// -----------------------------------------------------------------------------
// Drawing Helpers
// -----------------------------------------------------------------------------

void draw_muted_overlay(const FlyGuiItem& item)
{
    const int16_t x = item.x() + (item.width() - static_cast<int16_t>(SPRITE_MUTED_X_WIDTH)) / 2;
    const int16_t y = item.y() + (item.height() - static_cast<int16_t>(SPRITE_MUTED_X_HEIGHT)) / 2;
    SpriteDraw::drawPng(sprite_muted_x, SPRITE_MUTED_X_BYTES, x, y, SPRITE_MUTED_X_WIDTH, SPRITE_MUTED_X_HEIGHT, true);
}

uint16_t meter_color(uint8_t level)
{
    if (level >= 100)
    {
        return TFT_RED;
    }

    if (level <= 50)
    {
        const uint8_t blue = static_cast<uint8_t>(255U - (static_cast<uint16_t>(level) * 255U) / 50U);
        return thefly_display.color565(255, 255, blue);
    }

    const uint8_t green = static_cast<uint8_t>(255U - (static_cast<uint16_t>(level - 50U) * 255U) / 50U);
    return thefly_display.color565(255, green, 0);
}

} // namespace
