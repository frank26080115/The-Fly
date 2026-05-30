#include "WavPlayback.h"

#include <algorithm>
#include <mutex>
#include <new>
#include <string.h>

#include "AudioManager.h"
#include "MicroSdCard.h"
#include "esp_log.h"

namespace WavPlayback
{
namespace
{

constexpr const char* TAG = "WavPlayback";

constexpr uint32_t kSampleRateHz      = AudioManager::kSampleRateHz;
constexpr uint32_t kChannels          = 2;
constexpr uint32_t kBytesPerSample    = sizeof(int16_t);
constexpr uint32_t kBytesPerFrame     = kChannels * kBytesPerSample;
constexpr uint32_t kBytesPerSecond    = kSampleRateHz * kBytesPerFrame;
constexpr uint32_t kPumpTargetFrames  = kSampleRateHz / 2;
constexpr size_t   kReadChunkFrames   = 512;
constexpr size_t   kSpeakerWatermarkSamples = 240;
constexpr size_t   kInactiveZeroRunFrames = 8;

std::mutex g_mutex;
FsFile     g_file;
bool       g_active       = false;
bool       g_playing      = false;
bool       g_finished     = false;
bool       g_eof          = false;
uint64_t   g_file_size    = 0;
uint64_t   g_data_bytes   = 0;
uint64_t   g_position_bytes = 0;
uint8_t    g_volume       = AudioManager::kMaxVolume;
char       g_path[96]     = {};
char       g_error[96]    = {};
int16_t    g_read_buffer[kReadChunkFrames * kChannels];
int16_t    g_mono_buffer[kReadChunkFrames];
size_t     g_left_zero_run  = kInactiveZeroRunFrames;
size_t     g_right_zero_run = kInactiveZeroRunFrames;

uint64_t clamp_data_position(uint64_t positionBytes)
{
    if (positionBytes > g_data_bytes)
    {
        return g_data_bytes;
    }

    return positionBytes - (positionBytes % kBytesPerFrame);
}

uint64_t bytes_for_ms(uint32_t positionMs)
{
    const uint64_t frames = (static_cast<uint64_t>(positionMs) * kSampleRateHz) / 1000ULL;
    return clamp_data_position(frames * kBytesPerFrame);
}

uint32_t ms_for_bytes(uint64_t bytes)
{
    bytes = clamp_data_position(bytes);
    return static_cast<uint32_t>((bytes * 1000ULL) / kBytesPerSecond);
}

void set_error(const char* error)
{
    strlcpy(g_error, error ? error : "", sizeof(g_error));
}

void reset_channel_activity_locked()
{
    g_left_zero_run  = kInactiveZeroRunFrames;
    g_right_zero_run = kInactiveZeroRunFrames;
}

bool seek_locked(uint64_t positionBytes)
{
    positionBytes = clamp_data_position(positionBytes);
    const uint64_t filePosition = static_cast<uint64_t>(kWavHeaderBytes) + positionBytes;
    if (!g_file.seek(filePosition))
    {
        set_error("seek failed");
        return false;
    }

    g_position_bytes = positionBytes;
    g_eof            = g_position_bytes >= g_data_bytes;
    g_finished       = false;
    reset_channel_activity_locked();
    return true;
}

void clear_fifo_and_rewind_locked(bool rewindQueuedAudio)
{
    AudioFifo& fifo = AudioManager::bluetoothToSpeakerFifo();
    uint64_t   rewindBytes = 0;
    if (rewindQueuedAudio)
    {
        rewindBytes = static_cast<uint64_t>(fifo.usedSamples()) * kBytesPerFrame;
    }

    fifo.clear();
    fifo.setWatermark(kSpeakerWatermarkSamples);

    if (rewindBytes > 0)
    {
        const uint64_t nextPosition = rewindBytes > g_position_bytes ? 0 : g_position_bytes - rewindBytes;
        seek_locked(nextPosition);
    }
}

void finish_locked()
{
    g_playing  = false;
    g_finished = true;
    g_eof      = true;
    g_position_bytes = g_data_bytes;
    AudioManager::bluetoothToSpeakerFifo().setWatermark(kSpeakerWatermarkSamples);
}

int16_t mix_stereo_frame_to_mono(const int16_t* frame)
{
    const int16_t left  = frame[0];
    const int16_t right = frame[1];

    g_left_zero_run  = left == 0 ? g_left_zero_run + 1 : 0;
    g_right_zero_run = right == 0 ? g_right_zero_run + 1 : 0;

    const bool leftActive  = g_left_zero_run < kInactiveZeroRunFrames;
    const bool rightActive = g_right_zero_run < kInactiveZeroRunFrames;

    if (leftActive && rightActive)
    {
        return static_cast<int16_t>((static_cast<int32_t>(left) + static_cast<int32_t>(right)) / 2);
    }
    if (leftActive)
    {
        return left;
    }
    if (rightActive)
    {
        return right;
    }

    return 0;
}

size_t frames_available_to_queue()
{
    AudioFifo& fifo = AudioManager::bluetoothToSpeakerFifo();
    const size_t available = fifo.availableToWrite();
    return available >= kSpeakerWatermarkSamples ? available : 0;
}

} // namespace

bool start(const char* playbackPath)
{
    stop();

    std::lock_guard<std::mutex> lock(g_mutex);
    set_error("");

    if (!playbackPath || playbackPath[0] == '\0')
    {
        set_error("empty path");
        return false;
    }

    if (!MicroSdCard::isReady())
    {
        set_error("microSD not ready");
        return false;
    }

    strlcpy(g_path, playbackPath, sizeof(g_path));
    if (!g_file.open(g_path, O_RDONLY))
    {
        set_error("open failed");
        ESP_LOGW(TAG, "open failed: %s", g_path);
        g_path[0] = '\0';
        return false;
    }

    g_file_size      = g_file.fileSize();
    g_data_bytes     = g_file_size > kWavHeaderBytes ? g_file_size - kWavHeaderBytes : 0;
    g_data_bytes     = clamp_data_position(g_data_bytes);
    g_position_bytes = 0;
    g_eof            = g_data_bytes == 0;
    g_finished       = g_eof;
    g_playing        = !g_eof;
    g_active         = true;

    AudioFifo& fifo = AudioManager::bluetoothToSpeakerFifo();
    fifo.clear();
    fifo.setWatermark(kSpeakerWatermarkSamples);
    AudioManager::setSpeakerMuted(false);
    AudioManager::setVolume(g_volume);
    if (!AudioManager::enableSpeakerMode())
    {
        set_error("speaker failed");
        g_file.close();
        g_active = false;
        g_playing = false;
        g_path[0] = '\0';
        return false;
    }

    if (!seek_locked(0))
    {
        g_file.close();
        g_active = false;
        g_playing = false;
        g_path[0] = '\0';
        return false;
    }

    ESP_LOGI(TAG,
             "started: %s bytes=%llu data=%llu duration=%lu ms",
             g_path,
             static_cast<unsigned long long>(g_file_size),
             static_cast<unsigned long long>(g_data_bytes),
             static_cast<unsigned long>(ms_for_bytes(g_data_bytes)));
    return true;
}

void stop()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file)
    {
        g_file.close();
    }

    AudioFifo& fifo = AudioManager::bluetoothToSpeakerFifo();
    fifo.clear();
    fifo.setWatermark(kSpeakerWatermarkSamples);
    AudioManager::stop();

    g_active         = false;
    g_playing        = false;
    g_finished       = false;
    g_eof            = false;
    g_file_size      = 0;
    g_data_bytes     = 0;
    g_position_bytes = 0;
    g_path[0]        = '\0';
}

void pump()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_active || !g_playing || !g_file)
    {
        return;
    }

    AudioFifo& fifo = AudioManager::bluetoothToSpeakerFifo();
    if (g_eof)
    {
        fifo.setWatermark(0);
        if (fifo.usedSamples() == 0)
        {
            finish_locked();
        }
        return;
    }

    size_t framesRemainingThisPump = kPumpTargetFrames;
    while (framesRemainingThisPump > 0 && !g_eof)
    {
        const size_t availableFrames = frames_available_to_queue();
        if (availableFrames == 0)
        {
            break;
        }

        const uint64_t bytesRemaining = g_data_bytes > g_position_bytes ? g_data_bytes - g_position_bytes : 0;
        if (bytesRemaining == 0)
        {
            g_eof = true;
            fifo.setWatermark(0);
            break;
        }

        const size_t fileFramesRemaining = static_cast<size_t>(std::min<uint64_t>(bytesRemaining / kBytesPerFrame, UINT32_MAX));
        const size_t framesToRead = std::min({ kReadChunkFrames, framesRemainingThisPump, availableFrames, fileFramesRemaining });
        if (framesToRead == 0)
        {
            g_eof = true;
            fifo.setWatermark(0);
            break;
        }

        const size_t bytesToRead = framesToRead * kBytesPerFrame;
        const int    bytesRead   = g_file.read(g_read_buffer, bytesToRead);
        if (bytesRead <= 0)
        {
            g_eof = true;
            fifo.setWatermark(0);
            break;
        }

        const size_t framesRead = static_cast<size_t>(bytesRead) / kBytesPerFrame;
        if (framesRead == 0)
        {
            g_eof = true;
            fifo.setWatermark(0);
            break;
        }

        for (size_t i = 0; i < framesRead; ++i)
        {
            g_mono_buffer[i] = mix_stereo_frame_to_mono(&g_read_buffer[i * kChannels]);
        }

        const size_t queued = fifo.queue(g_mono_buffer, framesRead, kSampleRateHz);
        g_position_bytes += static_cast<uint64_t>(queued) * kBytesPerFrame;
        if (queued < framesRead)
        {
            const uint64_t rewind = static_cast<uint64_t>(framesRead - queued) * kBytesPerFrame;
            seek_locked(g_position_bytes);
            if (rewind > 0)
            {
                break;
            }
        }

        framesRemainingThisPump -= queued;
        if (static_cast<size_t>(bytesRead) < bytesToRead || g_position_bytes >= g_data_bytes)
        {
            g_eof = true;
            fifo.setWatermark(0);
            break;
        }
    }
}

bool active()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_active;
}

bool playing()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_active && g_playing;
}

bool paused()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_active && !g_playing;
}

bool finished()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_finished;
}

void setPlaying(bool shouldPlay)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_active)
    {
        return;
    }

    if (!shouldPlay)
    {
        if (g_playing)
        {
            clear_fifo_and_rewind_locked(true);
        }
        g_playing = false;
        return;
    }

    if (g_finished || g_position_bytes >= g_data_bytes)
    {
        seek_locked(0);
        AudioManager::bluetoothToSpeakerFifo().clear();
    }

    g_finished = false;
    g_eof      = g_position_bytes >= g_data_bytes;
    g_playing  = !g_eof;
    AudioManager::bluetoothToSpeakerFifo().setWatermark(kSpeakerWatermarkSamples);
}

void togglePlaying()
{
    setPlaying(!playing());
}

void setPositionMs(uint32_t newPositionMs)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_active)
    {
        return;
    }

    AudioFifo& fifo = AudioManager::bluetoothToSpeakerFifo();
    fifo.clear();
    fifo.setWatermark(kSpeakerWatermarkSamples);
    seek_locked(bytes_for_ms(newPositionMs));
    g_finished = false;
    if (g_position_bytes >= g_data_bytes)
    {
        g_eof = true;
        g_playing = false;
        g_finished = true;
    }
}

void setVolume(uint8_t nextVolume)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_volume = nextVolume > AudioManager::kMaxVolume ? AudioManager::kMaxVolume : nextVolume;
    AudioManager::setVolume(g_volume);
}

uint32_t durationMs()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return ms_for_bytes(g_data_bytes);
}

uint32_t positionMs()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    const uint32_t current = ms_for_bytes(g_position_bytes);
    const uint32_t total   = ms_for_bytes(g_data_bytes);
    return current > total ? total : current;
}

uint8_t volume()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_volume;
}

const char* path()
{
    return g_path;
}

const char* lastError()
{
    return g_error;
}

} // namespace WavPlayback
