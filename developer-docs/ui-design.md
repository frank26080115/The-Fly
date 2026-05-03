Things that the user needs to be able to do:

 * pair with Bluetooth host device
 * unpair Bluetooth
 * initiate Wi-Fi connection to access point
 * initiate Wi-Fi access point and wait for incoming connection
 * change volume during actual calls
 * mute or unmute mic
 * view list of files
 * set the time of the RTC

The screen will always have a 3 pixel wide margin on all sides, this will be used for a "tally light". If the mic is actively recording, this region will be completely red. If there is a call being recorded but the local mic is muted, then the screen border should be four dotted lines.

The "hotkeys" are buttons connected to GPIO (this is a future feature when I get around to making an extension circuit board). Each one can be for things like "memo", or "todo item", or "journal entry", etc. A short tap will start the recording for the hotkey's categorization. A long hold will start the recording and end the recording when the button is released.

The top of the screen will always show the current date and time, and the battery status

This document might have details superceeded by the design of `lib\FlyGui\FlyGui.h` and related items
