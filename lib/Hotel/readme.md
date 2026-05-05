`Hotel` is a power manager, responsible for dimming the screen, using light sleep modes when appropriate.

Implement as a namespace `Hotel`, as there is only one instance.

There needs to be two polling functions. One called from the main loop thread on core 0, and the other called from the main loop thread on core 1. The only reason for this, is so that if we actually do want to use light sleep, we want to do it when both cores are done their current tasks.

Throughout the code, any user interaction, such as a button-down or touch event, will reset an internal timer in hotel.

If we are in any active audio recording, bluetooth usage, or wi-fi usage, then do not do any power saving at all, full CPU speed, no sleep, full brightness.

If the internal timer says there has been user activity within the past 1 minute, then the screen backlight is at full brightness.

If the internal timer says there has been user activity within the past 10 seconds, then the CPU clock is at maximum speed. Otherwise, reduce the CPU clock speed to minimum required.

If the internal timer says there has NOT been user activity within the past 30 seconds, then we start to utilize light-sleep, with a periodic wake-up every 50 milliseconds or upon interrupt (I think the touch controller has an interrupt). The sleep can only happen when both polling functions are synchronized meaning both cores are not busy.

If the timer says there has been no activity for 5 minutes, the system will actually shutdown using the M5 PMIC call.

Make sure `millis()` works correctly in all cases, if this is difficult, implement a wrapper around `millis()`.

This all works using a state machine, actions can only occur during state transitions, no repeated redundant actions are allowed, all state transitions are logged with debug verbosity.

If implemented as one state machine, the states are:

- `Blocked`: active audio recording, Bluetooth usage, or Wi-Fi usage is in progress. Full CPU speed, no sleep, full brightness.
- `RecentlyActive`: user activity happened within the last 10 seconds. Full CPU speed and full brightness.
- `ActiveDimAllowed`: user activity happened within the last 30 seconds, but not within the last 10 seconds. Minimum required CPU speed and full brightness.
- `LightSleepReady`: user activity happened within the last 1 minute, but not within the last 30 seconds. Minimum required CPU speed, full brightness, and light sleep allowed when both core polling functions synchronize.
- `DimLightSleepReady`: user activity happened within the last 5 minutes, but not within the last 1 minute. Minimum required CPU speed, dimmed backlight, and light sleep allowed when both core polling functions synchronize.
- `Shutdown`: no user activity for 5 minutes. Entered once to shut down through the M5 PMIC call.
