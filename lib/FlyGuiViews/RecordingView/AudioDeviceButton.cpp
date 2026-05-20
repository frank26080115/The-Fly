#include "AudioDeviceButton.h"

#include "AudioManager.h"
#include "SpriteDraw.h"
#include "sprites.h"

namespace
{
constexpr int16_t kButtonSize = 100;

void draw_muted_overlay(const FlyGuiItem& item)
{
    const int16_t x = item.x() + (item.width() - static_cast<int16_t>(SPRIT_MUTED_X_WIDTH)) / 2;
    const int16_t y = item.y() + (item.height() - static_cast<int16_t>(SPRIT_MUTED_X_HEIGHT)) / 2;
    SpriteDraw::drawPng(sprit_muted_x, SPRIT_MUTED_X_BYTES, x, y, SPRIT_MUTED_X_WIDTH, SPRIT_MUTED_X_HEIGHT, true);
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

AudioDeviceButton::AudioDeviceButton(Device device, int16_t x, int16_t y)
    : FlyGuiItem(x, y, kButtonSize, kButtonSize),
      device_(device)
{
    if (device_ == Device::Mic)
    {
        setSprite(sprit_btn_mic, SPRIT_BTN_MIC_WIDTH, SPRIT_BTN_MIC_HEIGHT, SPRIT_BTN_MIC_BYTES);
    }
    else
    {
        setSprite(sprit_btn_speaker, SPRIT_BTN_SPEAKER_WIDTH, SPRIT_BTN_SPEAKER_HEIGHT, SPRIT_BTN_SPEAKER_BYTES);
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

void AudioDeviceButton::drawAudioMeter()
{
    if (!visible())
    {
        return;
    }

    const uint8_t level = device_ == Device::Mic ? AudioManager::micScaledPeakLevel() : AudioManager::speakerPeakLevel();
    const int16_t center = x() + width() / 2;
    const int16_t half = static_cast<int16_t>((static_cast<uint32_t>(width() / 2) * level) / 100U);

    thefly_display.drawFastHLine(x(), y(), width(), TFT_BLACK);
    if (half <= 0)
    {
        return;
    }

    thefly_display.drawFastHLine(center - half, y(), half * 2, meter_color(level));
}

void AudioDeviceButton::redraw(bool forced)
{
    const bool shouldDrawOverlay = visible() && (forced || dirty());
    FlyGuiItem::redraw(forced);
    if (shouldDrawOverlay && mutedOverlay_)
    {
        draw_muted_overlay(*this);
    }
    if (shouldDrawOverlay)
    {
        drawAudioMeter();
    }
}
