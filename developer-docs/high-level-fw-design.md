The build platform uses PlatformIO and the Espressif ESP32 Framework for Arduino

The physical device is a M5Stack Core 2, which is a ESP32 (classic variant) microcontroller internally

The two libraries from M5Stack:
 * https://github.com/m5stack/M5Unified/tree/0.2.14
 * https://github.com/m5stack/M5GFX/tree/0.2.20

There may be a WM8960 available, this is enabled or disabled via a build-time preprocessor flag. When the WM8960 is used, there is only 

If the WM8960 is not available, the M5Stack has both a microphone (SPM1423 via PDM) and a speaker (NS4168 via I2S). These share pins. A push-to-talk button will switch between the two.

For the graphical UI, the screen updates should be only-as-necessary as to not hog time writing to the screen.

The RTC should be used to provide a time stamp for all audio recordings.

Bluetooth will never be active when Wi-Fi is active. Audio recording is also not possible if Wi-Fi is active. There should never be any worry about file handles or concurrent access to multiple files or the file system changing state unexpectedly.

Assume the microSD card is permanently attached, always available. Unavailability is a fatal event and not able to recover from until reboot.

### File name conventions

All audio recordings are two files, one with a `.raw` extension which contains the actual audio, and another one with the same name but ends with `.meta.json`, containing any metadata.

The file name will look like `T-YYYY-MM-DD-HH-MM-SS-U`. Where `T` is a letter code representing the categorization of the recording. The rest is the time according to the RTC.

### Audio Data Formats

The Bluetooth audio path is HFP and negotiates one of two audio modes: CVSD/narrowband or mSBC/wideband.

For the local audio pipeline and recording format, both modes become signed 16-bit little-endian mono PCM.

When using CVSD, the local PCM sample rate is 8 kHz.

When using mSBC, the Bluetooth-side payload is mSBC/SBC encoded. After decoding, the local PCM sample rate is 16 kHz.

Avoid any volume manipulation of audio samples, relegate that to the DAC or amp which should have settings for volume.

### Buffer Sizes

 * CVSD / 8 kHz path:
   * If treated as 16-bit mono PCM: 8,000 samples/sec * 2 bytes = 16,000 bytes/sec
   * Typical 7.5 ms audio chunk: 60 samples * 2 = 120 bytes
   * Typical 15 ms chunk: 120 samples * 2 = 240 bytes
 * mSBC / 16 kHz path
   * Decoded PCM is 16-bit mono: 16,000 samples/sec * 2 bytes = 32,000 bytes/sec
   * mSBC frame represents 120 PCM samples: 120 * 2 = 240 bytes
   * Encoded mSBC frame is about 57-60 bytes, depending on framing/header.

So the largest normal per-frame callback-side working buffer is the decoded mSBC PCM block: about 240 bytes.

The current scratch buffers are: 512 bytes is comfortably enough for one mSBC decode/encode PCM block, and still enough if a callback chunk is closer to a 15 ms PCM window. If you want a conservative round number for future buffering, I’d use 1024 bytes per direction.

For file writing, the buffer sizes will be much bigger
