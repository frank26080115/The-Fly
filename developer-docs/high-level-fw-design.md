The build platform uses PlatformIO and the Espressif ESP32 Framework for Arduino

The physical device is a M5Stack Core 2, which is a ESP32 (classic variant) microcontroller internally

The two libraries from M5Stack:
 * https://github.com/m5stack/M5Unified/tree/0.2.14
 * https://github.com/m5stack/M5GFX/tree/0.2.20

There may be a WM8960 available, this is enabled or disabled via a build-time preprocessor flag. When the WM8960 is used, there is only 

If the WM8960 is not available, the M5Stack has both a microphone (SPM1423 via PDM) and a speaker (NS4168 via I2S). These share pins. A push-to-talk button will switch between the two.

For the graphical UI, the screen updates should be only-as-necessary as to not hog time writing to the screen.

The RTC should be used to provide a time stamp for all audio recordings.

### File name conventions

All audio recordings are two files, one with a `.raw` extension which contains the actual audio, and another one with the same name but ends with `.meta.json`, containing any metadata.

The file name will look like `T-YYYY-MM-DD-HH-MM-SS-U`. Where `T` is a letter code representing the categorization of the recording. The rest is the time according to the RTC.
