## HFP SDKConfig Requirements

The default ESP-IDF build did not include the correct options for HFP. Solved by using a custom sdkconfig

## IRAM0 Linker Pressure

The default ESP-IDF build resulted in too much IRAM0 usage and fails to link. The first attempted fix is to stop using PSRAM altogether, but then the PNG decoder would fail to work. So the real fix is to use custom sdkconfig to optimize the IRAM0 usage (and we also optimized a lot of other RAM usage)

## PlatformIO Configuration Cache Drift

Sometimes, PlatformIO forgets that there's a custom sdkconfig and builds without the settings that we need, we can catch this because the iram0 overflow error would halt the build.

The first fix is to delete the cached configs, completely delete `.pio/build`, and force PlatformIO to reconfigure and rebuild everything. This was extremely slow and annoying.

The next fix was to package up the library archive files and working config headers, and a new better linker script, put all of that in a zip file, host it as a platform_package on my own github account, and let PlatformIO download that package. This is bad though because it is essentially providing prebuilt binary files, so my documentation does still call out all the customizations and switching them into the `platformio.ini` file can cause the from-source build happen.

## Slow microSD Disk Statistics

Getting disk statistics from the microSD card is a very long process so we added DiskStats to cache the data only when needed and in a way invisible to the user.

## Phone Auto-Connect After Pairing

The phone really likes to connect to the device immediately after pairing, which is super annoying, but if we don't let it, the phone complains. The fix is to allow the connection invisibly and then do a reboot after pairing.

## Browser Crypto Without HTTPS

It is not possible to serve a HTTPS connection from the ESP32 without the user installing a certificate on the client computer/phone. Without HTTPS, WebCrypto (Javascript) is not available. The fix is to build a webcrypto-shim that's meant to bring the same functionality to outdated web browsers.

## Security Policy For HTTP Workflows

As using HTTP is insecure, I've set as a policy that functions requiring security can only happen on a soft-AP that is enforcing connecting only one client at a time, enforcing the usage of WPA3, and using a randomly generated SSID and randomly generated password.

## Audio File Format Change For Playback

The original firmware design used a custom audio file format that had timestamped audio chunks that are reassembled by a decoder python script. This had to change when I realized that on-device playback would be a key feature that users cannot live without, which is when we switched to using `wav` and encrypted wav-like file formats, which is way more easier to stream with duration calculations and seeking functions.

## MP3 Compression For Wi-Fi Transfers

The `wav` and wav-like encrypted files are too big and too slow to transfer over Wi-Fi (250kB/s speeds, it would take like 20 minutes just to download a 1 hour long meeting). So we switched to using MP3 compression to bring the file size down.

We also implemented silent-gap removal to keep the file sizes down even further.

## Constant Bitrate MP3 Requirement

MP3 compression must be configured for CBR or else the file duration is almost impossible to calculate.

## Replacing LAME With Shine

The `LAME` MP3 encoder had a realtime factor of about 1.04x in benchmark testing, and in real Bluetooth recordings, it was experiencing about 160 FIFO overflow events for a 30 second recording (this was really hard to detect by just listening though). We tried changing knobs like the bit rate and quality but could not make meaningful improvements. My options were to either mix the stereo data down to mono (bad idea as I want the transcriber to know that one channel might be a completely different person), or to change the encoder altogether. The solution was to implement the `shine` encoder instead, which uses a fixed-point math implementation. This had a real-time factor of 2.5x, solving the problem completely.

## Async Web Downloads And SPI Contention

The async web server was conflicting with the GUI during file downloads because the microSD card and the LCD screen both use the same SPI bus. During audio recording/playback this isn't a problem because I've put them on the same CPU core and the GUI redraws are rate limited. But with the async web server running on another CPU core, the GUI redraws had to be blocked during a file transfer.

## Microphone DC Offset And Low Gain

The microphone built into the M5Stack Core2 has a heavy DC offset and isn't very loud. To fix this, we implemented automatic gain control (AGC) with a high pass filter (HPF).

## Power Management Limits With Bluetooth

Bluetooth just doesn't work when our `Hotel` power manager tries to lower clock speed to save power. Also, `Hotel`'s sleep functions caused almost random RTC watchdog expiry crashes. I tried exploring ways of fixing this, multiple people on the internet have encountered this, it looked like while some fixes might fix particular instances of it happening, there's no guarantee that the problem won't randomly reappear. So now `Hotel` can only dim the screen and fully shutdown, there's no CPU frequency scaling or light-sleep capability in it.

## Key Derivation Watchdog Risk

Key derivation on the ESP32 is extremely slow, and can cause the WDT to trip. There's only one place where it is absolutely needed. The fix is to limit the number of iterations, and also to pause once in a while to feed the WDT.
