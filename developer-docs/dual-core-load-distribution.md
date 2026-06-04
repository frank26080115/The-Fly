# Dual-core load distribution

The ESP32 in the M5Stack Core2 has two application-visible cores:

* Core 0 / PRO CPU: commonly used by ESP-IDF radio, timer, and system work.
* Core 1 / APP CPU: the Arduino `setup()` / `loop()` task normally runs here.

The firmware should treat audio as the real-time path and UI/storage as consumers of buffered data. Audio callbacks and I2S transfers must never wait on the screen, SD card, JSON parsing, Wi-Fi, or cloud upload.

## Strategy

The GUI can have blocking display updates. The SD card can also block, especially when a file write crosses a cache flush, FAT update, directory update, erase block, or card-internal garbage collection boundary. These operations do not need to run in the same task as audio capture/playback.

The main split is:

* Keep Bluetooth, HFP callbacks, I2S reads/writes, SBC encode/decode, and small audio packet assembly on the lowest-latency path.
* Move file writes, UI rendering, MP3 compression, and other high-latency operations out of the audio callback/task.
* Connect the two sides with queues or ring buffers sized for the worst expected SD stalls.

Do not write to SD or update the screen from an HFP data callback. A callback should copy or transform a small chunk, push it into a queue/ring buffer, and return.

## Recommended ownership

### Core 0

Use Core 0 for radio-adjacent and low-latency audio transport work:

* Bluetooth controller and Bluedroid tasks owned by ESP-IDF
* HFP event callbacks
* HFP data callbacks
* SCO/HCI packet handling
* SBC encode/decode if it is done directly in the HFP data path
* short I2S endpoint operations needed by the active audio path

Application code pinned to Core 0 should be minimal. If an app task is pinned here, it should be a small audio pump with no filesystem, display, Wi-Fi, heap-heavy JSON work, or long logging loops.

### Core 1

Use Core 1 for application-owned work that can block or run at human-speed:

* Arduino `setup()` / `loop()`
* UI state machine
* M5GFX screen rendering
* button handling
* MP3 compression
* SD card file write

The SD writer and UI can both live on Core 1, but they should be separate tasks. The SD writer should have higher priority than the UI while recording.

## Update June 2026

As we have added MP3 compression, the file sizes have shrunk significantly, and so the microSD card writes are no longer the primary concern for core 1 tasks. But the MP3 compression is actually very heavy in terms of compute time, and so the task allocation remains the same.
