#include "BluetoothDeviceView.h"

BluetoothDeviceView::BluetoothDeviceView()
    : // Design: bluetooth subscreen chooses which bluetooth device to connect to.
      FlyGuiView(FLYGUI_VIEW_BLUETOOTH)
{
}
