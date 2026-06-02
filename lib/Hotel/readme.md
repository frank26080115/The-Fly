`Hotel` is the device power manager. In normal firmware builds it dims the
screen after inactivity and shuts the device down after a longer idle period.

Implement as a namespace `Hotel`, as there is only one instance.

There are two polling functions: one called from the loop task on core 0, and
one called from the Arduino loop task on core 1. This lets Hotel coordinate
future sleep behavior across both cores, but normal builds do not currently
enter light sleep.

Throughout the code, any user interaction, such as a button-down or touch
event, resets Hotel's internal activity timer.

If audio recording, microphone capture, Bluetooth usage, or Wi-Fi usage is
active, Hotel enters `Blocked`: no dimming, no light sleep, no shutdown.
File playback is different: actively playing file playback may dim the screen,
but it still suppresses light sleep and shutdown. Paused file playback does not
suppress shutdown.

If there has been user activity within the past 1 minute, the screen backlight
stays at full brightness. After 1 minute, the screen is dimmed.

If there has been no activity for 5 minutes, the system shuts down using the M5
PMIC call. Shutdown is suppressed when logging is more verbose than error, so
debug builds do not silently power off during investigation.

CPU frequency scaling and light sleep are behind `ENABLE_HOTEL_DEEP_POWER_SAVE`.
Leave this undefined for normal builds. CPU frequency changes have interfered
with Bluetooth HFP setup, and Core2/M5Unified light sleep has caused RTCWDT
resets after idle.

Hotel is implemented as a state machine. Actions only occur during state
transitions, repeated redundant actions are avoided, and state transitions are
logged with debug verbosity.

Current states:

- `Blocked`: audio recording, microphone capture, Bluetooth, or Wi-Fi is active. Full brightness.
- `RecentlyActive`: user activity happened within the last 10 seconds. Full brightness.
- `ActiveDimAllowed`: user activity happened within the last 30 seconds, but not within the last 10 seconds. Full brightness.
- `LightSleepReady`: user activity happened within the last 1 minute, but not within the last 30 seconds. Full brightness; optional deep power save may sleep here.
- `DimLightSleepReady`: user activity happened within the last 5 minutes, but not within the last 1 minute. Dimmed brightness; optional deep power save may sleep here.
- `Shutdown`: no user activity for 5 minutes. Entered once to shut down through the M5 PMIC call.
