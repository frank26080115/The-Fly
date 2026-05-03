#include "ApModeView.h"

ApModeView::ApModeView()
    : // Design: AP mode screen idles while the FTP server runs and bottom buttons can stop it.
      FlyGuiView(FLYGUI_VIEW_AP_MODE)
{
}
