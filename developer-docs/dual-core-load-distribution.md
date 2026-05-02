# Dual-core load distribution

The ESP32 in the M5Stack Core2 has two application-visible cores:

* Core 0 / PRO CPU: commonly used by ESP-IDF radio, timer, and system work.
* Core 1 / APP CPU: the Arduino `setup()` / `loop()` task normally runs here.

The firmware should treat audio as the real-time path and UI/storage as consumers of buffered data. Audio callbacks and I2S transfers must never wait on the screen, SD card, JSON parsing, Wi-Fi, or cloud upload.

## Strategy

The GUI can have blocking display updates. The SD card can also block, especially when a file write crosses a cache flush, FAT update, directory update, erase block, or card-internal garbage collection boundary. These operations do not need to run in the same task as audio capture/playback.

The main split is:

* Keep Bluetooth, HFP callbacks, I2S reads/writes, SBC encode/decode, and small audio packet assembly on the lowest-latency path.
* Move file writes, metadata writes, UI rendering, FTP, HTTP upload, JSON config, and other high-latency operations out of the audio callback/task.
* Connect the two sides with queues or ring buffers sized for the worst expected SD stalls.

Do not write to SD or update the screen from an HFP data callback. A callback should copy or transform a small chunk, push it into a queue/ring buffer, and return.

## Unknowns to measure

It is not guaranteed from the application code alone which core runs the functions registered by `esp_hf_client_register_data_callback()`. Add temporary logging while profiling:

```cpp
ESP_LOGI(TAG, "hfp callback core=%d", xPortGetCoreID());
```

Log this once per callback type, not for every packet. Also log:

* core ID for the Arduino `setup()` / `loop()` task
* core ID for the file writer task
* core ID for the UI task
* maximum observed time spent in `file.write()`, `file.flush()`, and screen updates
* queue high-water marks and dropped audio packet count

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

Current example: `bt_audio_demo.cpp` pins `hfp_tx_pump` to Core 0. That is acceptable because it only wakes the HFP outgoing path with `esp_hf_client_outgoing_data_ready()` and sleeps briefly.

### Core 1

Use Core 1 for application-owned work that can block or run at human-speed:

* Arduino `setup()` / `loop()`
* UI state machine
* M5GFX screen rendering
* button handling
* SD card file writer task
* metadata `.meta.json` generation
* config file parsing
* FTP server when Wi-Fi mode is active
* HTTP upload when Wi-Fi mode is active

The SD writer and UI can both live on Core 1, but they should be separate tasks. The SD writer should have higher priority than the UI while recording.

## Suggested task layout

### Audio RX/TX callback

Pinned by the stack or system, not assumed by the app.

Responsibilities:

* accept the Bluetooth or I2S audio chunk
* do only required small-format conversion
* push a packet descriptor or PCM bytes into an audio ring buffer
* return quickly

Forbidden:

* `File.write()`
* `file.flush()`
* display calls
* JSON allocation/parsing
* network calls
* waiting on mutexes held by UI/storage

### Audio pump task

Preferred core: Core 0 if it directly feeds Bluetooth/HFP timing; Core 1 is acceptable for local-only microphone recording if I2S DMA buffering is large enough.

Responsibilities:

* read from I2S when not handled directly by the callback
* feed outgoing HFP readiness
* maintain audio packet timing
* count overruns/underruns

Priority: higher than UI and storage.

### Recorder writer task

Preferred core: Core 1.

Responsibilities:

* own the open recording file handle
* write queued audio packets to the pre-grown `.raw` file
* track actual recorded byte count
* flush on stop, truncate to actual length, and close
* write the `.meta.json` file after audio is closed

Priority: above UI, below audio.

The writer should write larger contiguous chunks when possible. A target chunk size of 8 KB to 64 KB is reasonable for SD throughput. The audio side should not care whether a particular write blocks; it should only care whether the queue is filling.

### UI task

Preferred core: Core 1.

Responsibilities:

* handle buttons/touch
* draw only changed regions
* show recording state, free space, errors, and upload state
* avoid long full-screen redraws while recording

Priority: below the recorder writer during recording.

The red recording border/tally indicator should be cheap to update and should not require a full-screen redraw.

### Wi-Fi / FTP / upload task

Preferred core: Core 1, active only when Bluetooth/audio recording is inactive.

The high-level design says Bluetooth is never active when Wi-Fi is active, and audio recording is not possible if Wi-Fi is active. This keeps RF coexistence and file ownership much simpler. Upload and FTP can therefore use simpler blocking filesystem/network code as long as the UI remains responsive enough.

## Queues and buffering

The queue between audio and storage is the important boundary.

For 16 kHz signed 16-bit mono PCM:

* 16,000 samples/sec
* 2 bytes/sample
* 32,000 bytes/sec

A 512 KB queue holds about 16 seconds of mono 16 kHz PCM. That is generous for normal SD stalls. A smaller first target, such as 128 KB to 256 KB, is probably enough, but the firmware should expose queue high-water marks so this can be tuned from real cards.

For call recording with both upstream and downstream PCM at 16 kHz, budget about 64,000 bytes/sec before packet headers. A 512 KB queue then holds about 8 seconds.

The queue should store packet boundaries if the recording format needs direction, timestamp, codec, flags, or event markers. If the raw file is temporarily plain PCM for demos, a byte ring buffer is fine.

## Locking rules

Avoid shared mutable state across cores. Prefer message passing.

Rules:

* The recorder writer owns the file handle.
* The UI reads status snapshots, not live writer internals.
* The audio path never waits for the UI.
* The audio path never waits for the SD writer except through non-blocking queue push/drop accounting.
* Runtime settings changed by the UI should be sent as commands to the owning task.
* Shared counters should be atomic or updated by one task and copied into a status struct.

## SD card behavior

`File.write()` returning a full byte count only means the bytes were accepted by the software stack. It does not prove the SD card's internal flash has completed programming. Timing can still be useful:

* short `write()` time usually means the data was buffered or an easy sector write completed
* long `write()` time often means a filesystem flush, FAT update, directory update, allocation, erase, or card-internal stall occurred
* `flush()` or `fsync()` timing is a better signal for durability-related stalls, but still not proof of physical NAND commit

For recording, the design should rely on buffering and pre-grown files, not on predicting exact SD stall timing.

## Priorities

Initial priority ordering:

* HFP callback / ESP-IDF Bluetooth tasks: stack-owned
* audio pump: high
* recorder writer: medium-high while recording
* UI: medium-low
* FTP/upload/config parsing: low or only active when not recording

Avoid raising UI priority to fix perceived sluggishness. First reduce redraw work and update only changed regions.

## Practical default

Start with this layout:

* Core 0: Bluetooth stack, HFP callbacks, HFP TX pump, tiny audio callback work.
* Core 1: Arduino app, UI task, SD writer task, config/metadata work.
* Wi-Fi/FTP/upload: Core 1 only, and only when Bluetooth/recording is stopped.

Then verify with core-ID logs, write/flush timing, queue high-water marks, and underrun/overrun counters on real microSD cards.
