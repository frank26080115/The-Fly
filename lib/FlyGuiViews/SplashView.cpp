#include "SplashView.h"

#include <LittleFS.h>

static constexpr const char* kSplashPath    = "/splash.png";
static constexpr uint32_t    kSplashDelayMs = 800;
static constexpr int32_t     kSplashWidth   = 320;
static constexpr int32_t     kSplashHeight  = 240;

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

    if (!LittleFS.begin(false) || !display.drawPngFile(LittleFS, kSplashPath, 0, 0, kSplashWidth, kSplashHeight))
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
