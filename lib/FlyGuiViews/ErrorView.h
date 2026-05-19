#pragma once

#include "../FlyGui/FlyGui.h"

class ErrorView : public FlyGuiView
{
public:
    ErrorView();

    void setMessage(const char* message, bool fatal);
    bool dismissed() const
    {
        return dismissed_;
    }
    bool fatal() const
    {
        return fatal_;
    }

    void onLoad() override;
    bool handleTouch(const FlyGuiTouchEvent& event) override;
    void redraw(M5GFX& display, bool forced) override;

private:
    static constexpr size_t kMessageMax = 255;

    char message_[kMessageMax + 1] = {};
    bool fatal_                    = true;
    bool dismissed_                = false;
};
