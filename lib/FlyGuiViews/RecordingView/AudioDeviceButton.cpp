#include "AudioDeviceButton.h"

#include <pgmspace.h>

#include "AudioManager.h"
#include "SpriteDraw.h"
#include "sprites.h"

namespace
{
constexpr int16_t kButtonSize = 100;

static const uint8_t kDbMeterLevelByLinearPercent[101] PROGMEM = {
    0,  0,  2,  6,  9,  12, 15, 18, 20, 23, 25, 27, 29, 31, 33, 35,
    36, 38, 39, 41, 42, 44, 45, 46, 48, 49, 50, 51, 52, 53, 55, 56,
    57, 58, 59, 60, 61, 61, 62, 63, 64, 65, 66, 67, 68, 68, 69, 70,
    71, 71, 72, 73, 74, 74, 75, 76, 76, 77, 78, 78, 79, 80, 80, 81,
    82, 82, 83, 83, 84, 85, 85, 86, 86, 87, 87, 88, 88, 89, 90, 90,
    91, 91, 92, 92, 93, 93, 94, 94, 95, 95, 95, 96, 96, 97, 97, 98,
    98, 99, 99, 100, 100,
};

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static void     draw_muted_overlay(const FlyGuiItem& item);
static uint8_t  db_meter_level(uint8_t linearLevel);
static uint16_t meter_color(uint8_t level);
} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

AudioDeviceButton::AudioDeviceButton(Device device, int16_t x, int16_t y)
    : FlyGuiItem(x, y, kButtonSize, kButtonSize), device_(device)
{
    syncBaseSprite();
}

void AudioDeviceButton::setEarVariant(bool earVariant)
{
    if (earVariant_ == earVariant)
    {
        return;
    }

    earVariant_ = earVariant;
    syncBaseSprite();
    if (owner())
    {
        owner()->setDirty();
    }
}

void AudioDeviceButton::syncBaseSprite()
{
    if (device_ == Device::Mic && earVariant_)
    {
        setSprite(sprite_micear_100, SPRITE_MICEAR_100_WIDTH, SPRITE_MICEAR_100_HEIGHT, SPRITE_MICEAR_100_BYTES);
    }
    else if (device_ == Device::Mic)
    {
        setSprite(sprite_mic_100, SPRITE_MIC_100_WIDTH, SPRITE_MIC_100_HEIGHT, SPRITE_MIC_100_BYTES);
    }
    else if (earVariant_)
    {
        setSprite(sprite_speakerear_100,
                  SPRITE_SPEAKEREAR_100_WIDTH,
                  SPRITE_SPEAKEREAR_100_HEIGHT,
                  SPRITE_SPEAKEREAR_100_BYTES);
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

    const uint8_t linearLevel =
        device_ == Device::Mic ? AudioManager::micScaledPeakLevel() : AudioManager::speakerPeakLevel();
    const uint8_t level = db_meter_level(linearLevel);
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

uint8_t db_meter_level(uint8_t linearLevel)
{
    const uint8_t clamped = linearLevel > 100 ? 100 : linearLevel;
    return pgm_read_byte(&kDbMeterLevelByLinearPercent[clamped]);
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
