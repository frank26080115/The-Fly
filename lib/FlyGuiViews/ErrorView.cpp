#include "ErrorView.h"

#include "FlyGuiText.h"
#include "SpriteDraw.h"
#include "WebServer.h"
#include "WifiManager.h"
#include "dbg_log.h"
#include "sprites.h"
#include <string.h>

extern WifiManager* wifi_manager;

namespace
{
constexpr const char* TAG             = "ErrorView";
constexpr int16_t     kIconY          = FlyGui::kTopBarHeight + 16;
constexpr int16_t     kTextX          = 4;
constexpr int16_t     kTextGap        = 5;
constexpr int16_t     kTextPadding    = 4;
constexpr int16_t     kTextLineHeight = 17;
constexpr int16_t     kActionSize     = 50;
constexpr int16_t     kActionY        = 188;
constexpr float       kTextSize       = 1.0f;
constexpr uint8_t     kTextFont       = 2;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static int16_t action_x();
} // namespace

ErrorView* ErrorView::activeView_ = nullptr;

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

ErrorView::ErrorView() : FlyGuiView(FLYGUI_VIEW_ERROR), actionItem_(0, kActionY, kActionSize, kActionSize)
{
    actionItem_.setCallback(onActionTriggered);
    addItem(actionItem_);
}

void ErrorView::setMessage(const char* message, bool fatal)
{
    strncpy(message_, message ? message : "", sizeof(message_) - 1);
    message_[sizeof(message_) - 1] = '\0';
    fatal_                         = fatal;
    dismissed_                     = false;
    setDirty();
}

void ErrorView::onLoad()
{
    activeView_ = this;
    dismissed_  = false;
    syncActionButton();
    FlyGuiView::onLoad();
}

void ErrorView::onUnload()
{
    if (activeView_ == this)
    {
        activeView_ = nullptr;
    }
    FlyGuiView::onUnload();
}

bool ErrorView::handleTouch(const FlyGuiTouchEvent& event)
{
    if (actionItem_.visible() && (actionItem_.contains(event.x, event.y) || actionItem_.isPressed()))
    {
        return actionItem_.handleTouch(event);
    }

    return true;
}

void ErrorView::onPressRight()
{
    if (actionItem_.visible())
    {
        actionItem_.trigger();
    }
}

void ErrorView::redraw(bool forced)
{
    if (!forced && !dirty())
    {
        return;
    }

    syncActionButton();
    thefly_display.fillScreen(TFT_BLACK);
    if (gui())
    {
        gui()->requestTopBarFullRedraw();
    }

    const int16_t icon_x =
        static_cast<int16_t>((thefly_display.width() - static_cast<int32_t>(SPRITE_WARNING_100_WIDTH)) / 2);
    SpriteDraw::drawPng(sprite_warning_100,
                        SPRITE_WARNING_100_BYTES,
                        icon_x,
                        kIconY,
                        SPRITE_WARNING_100_WIDTH,
                        SPRITE_WARNING_100_HEIGHT,
                        true);

    thefly_display.setTextFont(kTextFont);
    thefly_display.setTextSize(kTextSize);
    thefly_display.setTextDatum(top_left);
    thefly_display.setTextColor(TFT_WHITE, TFT_BLACK);

    const int16_t text_y     = static_cast<int16_t>(kIconY + SPRITE_WARNING_100_HEIGHT + kTextGap);
    const int16_t text_max_y = actionItem_.visible() ? static_cast<int16_t>(kActionY - kTextPadding)
                                                     : static_cast<int16_t>(thefly_display.height() - kTextPadding);
    FlyGuiTextUtil::drawWrappedText(message_,
                                    kTextX,
                                    text_y,
                                    static_cast<int16_t>(thefly_display.width() - (kTextX * 2)),
                                    text_max_y,
                                    kTextLineHeight);

    if (actionItem_.visible())
    {
        actionItem_.redraw(true);
    }

    markClean();
}

void ErrorView::onActionTriggered(uint32_t)
{
    if (!activeView_)
    {
        return;
    }

    if (activeView_->fatal_)
    {
        activeView_->launchDefaultSoftAp();
        return;
    }

    activeView_->dismiss();
}

void ErrorView::syncActionButton()
{
    actionItem_.relocate(action_x(), kActionY, kActionSize, kActionSize);
    if (fatal_)
    {
        actionItem_.setVisible(wifi_manager != nullptr);
        actionItem_.setSprite(sprite_firstaidwifi_50,
                              SPRITE_FIRSTAIDWIFI_50_WIDTH,
                              SPRITE_FIRSTAIDWIFI_50_HEIGHT,
                              SPRITE_FIRSTAIDWIFI_50_BYTES);
        return;
    }

    actionItem_.setVisible(true);
    actionItem_.setSprite(sprite_doorreturn_50,
                          SPRITE_DOORRETURN_50_WIDTH,
                          SPRITE_DOORRETURN_50_HEIGHT,
                          SPRITE_DOORRETURN_50_BYTES);
}

void ErrorView::dismiss()
{
    dismissed_ = true;
}

void ErrorView::launchDefaultSoftAp()
{
    if (!wifi_manager)
    {
        return;
    }

    const bool ap_ready = wifi_manager->isGeneratedSoftApActive() || wifi_manager->startGeneratedSoftAp();
    if (!ap_ready)
    {
        DBG_LOGW(TAG, "emergency default SoftAP start failed: %s", wifi_manager->statusName());
        return;
    }

    if (!WebServer::init())
    {
        DBG_LOGW(TAG, "emergency web server start failed");
    }

    FlyGui* owner = gui();
    if (owner && !owner->showView(FLYGUI_VIEW_AP_MODE))
    {
        DBG_LOGW(TAG, "emergency AP view show failed");
    }
}

namespace
{

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

int16_t action_x()
{
    return static_cast<int16_t>((thefly_display.width() - kActionSize) / 2);
}

} // namespace
