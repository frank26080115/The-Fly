#pragma once

#include "../FlyGui/FlyGui.h"
#include <stddef.h>
#include <stdint.h>

using ModalDialogDismissCallback = void (*)();

// ModalDialog may seem redundant with FlyGuiModal
// FlyGuiModal is only used if we absolutely must not exit out of the current view (which will cause unloading)
// ModalDialog is used when a dialog is a part of a flow
class ModalDialog : public FlyGuiView
{
public:
    ModalDialog();

    void configure(const uint8_t*             spriteData,
                   size_t                     spriteBytes,
                   uint32_t                   spriteWidth,
                   uint32_t                   spriteHeight,
                   const char*                text,
                   uint16_t                   nextViewId,
                   ModalDialogDismissCallback dismissCallback = nullptr);

    bool configured() const
    {
        return configured_;
    }

    void onLoad() override;
    void onUnload() override;
    bool handleTouch(const FlyGuiTouchEvent& event) override;
    void redraw(bool forced) override;
    void onPressLeft() override;
    void onPressMid() override;
    void onPressRight() override;

private:
    static constexpr size_t kTextMax = 255;

    void dismiss();
    void clearConfiguration();
    void drawSprite();
    void drawText();
    void drawFooter();

    const uint8_t*             spriteData_         = nullptr;
    size_t                     spriteBytes_        = 0;
    uint32_t                   spriteWidth_        = 0;
    uint32_t                   spriteHeight_       = 0;
    char                       text_[kTextMax + 1] = {};
    uint16_t                   nextViewId_         = FLYGUI_VIEW_MAIN;
    ModalDialogDismissCallback dismissCallback_    = nullptr;
    bool                       configured_         = false;
};
