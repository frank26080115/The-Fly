This class imitates the RTC class but tracks the current time using the system `millis()` counter, to reduce the need for I2C transactions. It should be a drop in replacement for the RTC class provided for the M5Stack.

It takes the time from the RTC when a sync function is called (at boot, and at when the RTC is edited)

Then, it simply provides the current time based on the internal system millis() timer

Any attempts to write RTC data will invoke the actual RTC class write functions
