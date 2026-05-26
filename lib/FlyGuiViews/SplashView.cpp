#include "SplashView.h"

#include "SpriteDraw.h"
#include "sprites.h"

static constexpr uint32_t    kSplashDelayMs = 800;

SplashView::SplashView() : FlyGuiView(FLYGUI_VIEW_SPLASH) {}

void SplashView::onLoad()
{
    FlyGuiView::onLoad();
}

void SplashView::onUnload()
{
    FlyGuiView::onUnload();
}

bool SplashView::handleTouch(const FlyGuiTouchEvent& event)
{
    if (event.justPressed || event.justReleased)
    {
        if (gui())
        {
            gui()->showView(FLYGUI_VIEW_MAIN);
        }
    }

    return true;
}

void SplashView::redraw(bool forced)
{
    if (!forced && !dirty())
    {
        return;
    }

    const SpriteDraw::DrawResult result =
        SpriteDraw::drawPng(sprit_splash, SPRIT_SPLASH_BYTES, 0, 0, SPRIT_SPLASH_WIDTH, SPRIT_SPLASH_HEIGHT, true);
    if (!result.ok)
    {
        thefly_display.fillScreen(TFT_BLACK);
    }

    markClean();
}
