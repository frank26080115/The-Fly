#include "SplashView.h"

#include "SpriteDraw.h"
#include "sprites.h"

static constexpr uint32_t    kSplashDelayMs = 800;

SplashView::SplashView() : FlyGuiView(FLYGUI_VIEW_SPLASH) {}

void SplashView::onLoad()
{
    handedOff_ = false;
    FlyGuiView::onLoad();
}

void SplashView::redraw(M5GFX& display, bool forced)
{
    if (!forced && !dirty())
    {
        return;
    }

    const SpriteDraw::DrawResult result =
        SpriteDraw::drawPng(display, sprit_splash, SPRIT_SPLASH_BYTES, 0, 0, SPRIT_SPLASH_WIDTH, SPRIT_SPLASH_HEIGHT, true);
    if (!result.ok)
    {
        display.fillScreen(TFT_BLACK);
    }

    markClean();

    if (!handedOff_)
    {
        handedOff_ = true;
        delay(kSplashDelayMs);
        if (gui())
        {
            gui()->showView(FLYGUI_VIEW_MAIN);
        }
    }
}
