#pragma once

#include "../FlyGui/FlyGui.h"
#include <stddef.h>

class QrCodeView : public FlyGuiView
{
public:
    QrCodeView();

    void configure(const char* text);

    void onLoad() override;
    bool handleTouch(const FlyGuiTouchEvent& event) override;
    bool redraw(bool forced) override;
    void onPressLeft() override;
    void onPressMid() override;
    void onPressRight() override;

private:
    static constexpr size_t kTextMax = 383;

    void dismiss();
    bool drawQrCode();

    char text_[kTextMax + 1] = {};
};
