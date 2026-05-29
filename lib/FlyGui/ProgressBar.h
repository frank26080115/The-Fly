#pragma once

#include "FlyGui.h"

class ProgressBar : public FlyGuiItem
{
public:
    ProgressBar(int16_t x, int16_t y, int16_t width, int16_t height);

    void  firstDraw();
    void  update(float progress);
    float progress() const
    {
        return progress_;
    }

    void onLoad() override;
    void redraw(bool forced) override;

private:
    static float normalizeProgress(float progress);

    void    drawFrame() const;
    void    drawFill() const;
    int16_t filledWidth() const;
    uint16_t fillColor() const;

    float progress_ = 0.0f;
    bool  drawn_    = false;
};
