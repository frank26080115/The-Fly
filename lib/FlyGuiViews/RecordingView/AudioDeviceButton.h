#pragma once

#include "../../FlyGui/FlyGui.h"

class AudioDeviceButton : public FlyGuiItem
{
public:
    enum class Device
    {
        Mic,
        Speaker,
    };

    AudioDeviceButton(Device device, int16_t x, int16_t y);

    void setEarVariant(bool earVariant);
    void setMicMode(bool micMode);
    void setMutedOverlay(bool muted);
    bool drawAudioMeter();
    bool redraw(bool forced) override;

private:
    void syncBaseSprite();

    Device device_;
    bool   earVariant_   = false;
    bool   mutedOverlay_ = false;
};
