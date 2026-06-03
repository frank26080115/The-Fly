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
    void onUnload() override;
    bool handleTouch(const FlyGuiTouchEvent& event) override;
    bool redraw(bool forced) override;
    void onPressRight() override;

private:
    static constexpr size_t kMessageMax = 255;

    static void onActionTriggered(uint32_t pressDurationMs);

    void syncActionButton();
    void dismiss();
    void launchDefaultSoftAp();

    char       message_[kMessageMax + 1] = {};
    bool       fatal_                    = true;
    bool       dismissed_                = false;
    FlyGuiItem actionItem_;

    static ErrorView* activeView_;
};
