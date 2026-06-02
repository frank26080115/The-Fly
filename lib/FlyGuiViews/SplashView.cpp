#include "SplashView.h"

#include "SpriteDraw.h"
#include "sprites.h"

static constexpr uint32_t kSplashDelayMs = 800;

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

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
        SpriteDraw::drawPng(sprite_splash, SPRITE_SPLASH_BYTES, 0, 0, SPRITE_SPLASH_WIDTH, SPRITE_SPLASH_HEIGHT, true);
    if (!result.ok)
    {
        thefly_display.fillScreen(TFT_BLACK);
    }

    markClean();
}
