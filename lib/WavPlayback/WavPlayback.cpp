// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "WavPlayback.h"

#include <algorithm>
#include <string.h>

namespace
{

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

uint32_t read_le32(const uint8_t* data);
bool     wav_header_valid(const uint8_t* header);

} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

const char* WavPlayback::tag() const
{
    return "WavPlayback";
}

bool WavPlayback::beginSource()
{
    file_size_      = file().fileSize();
    data_bytes_     = 0;
    position_bytes_ = 0;

    uint8_t header[kWavHeaderBytes] = {};
    if (!file().seekSet(0) || file().read(header, sizeof(header)) != static_cast<int>(sizeof(header)))
    {
        setError("WAV header read failed");
        return false;
    }
    if (!wav_header_valid(header))
    {
        setError("bad WAV header");
        return false;
    }

    const uint64_t file_payload_bytes   = file_size_ > kWavHeaderBytes ? file_size_ - kWavHeaderBytes : 0;
    const uint64_t header_payload_bytes = read_le32(header + 40);
    data_bytes_                         = std::min(file_payload_bytes, header_payload_bytes);
    data_bytes_                         = clampDataPosition(data_bytes_);
    return seekDataPosition(0);
}

bool WavPlayback::seekToTimeMs(uint32_t positionMs)
{
    return seekDataPosition(bytesForMs(positionMs));
}

bool WavPlayback::pumpSource(size_t maxFrames)
{
    size_t frames_remaining_this_pump = maxFrames;
    while (frames_remaining_this_pump > 0 && !sourceAtEnd())
    {
        const size_t available_frames = framesAvailableToQueue();
        if (available_frames == 0)
        {
            break;
        }

        const uint64_t bytes_remaining = data_bytes_ > position_bytes_ ? data_bytes_ - position_bytes_ : 0;
        if (bytes_remaining == 0)
        {
            markEof();
            break;
        }

        const size_t file_frames_remaining =
            static_cast<size_t>(std::min<uint64_t>(bytes_remaining / kBytesPerFrame, UINT32_MAX));
        const size_t frames_to_read =
            std::min({kReadChunkFrames, frames_remaining_this_pump, available_frames, file_frames_remaining});
        if (frames_to_read == 0)
        {
            markEof();
            break;
        }

        size_t frames_read = 0;
        if (!readFrames(read_buffer_, frames_to_read, frames_read) || frames_read == 0)
        {
            markEof();
            break;
        }

        const size_t queued = queuePcmFrames(read_buffer_, frames_read, kChannels, kSampleRateHz);
        position_bytes_ += static_cast<uint64_t>(queued) * kBytesPerFrame;
        frames_remaining_this_pump -= queued;

        if (queued < frames_read)
        {
            seekDataPosition(position_bytes_);
            break;
        }
        if (frames_read < frames_to_read || position_bytes_ >= data_bytes_)
        {
            markEof();
            break;
        }
    }

    return true;
}

uint32_t WavPlayback::sourceDurationMs() const
{
    return msForBytes(data_bytes_);
}

uint32_t WavPlayback::sourcePositionMs() const
{
    return msForBytes(position_bytes_);
}

bool WavPlayback::sourceAtEnd() const
{
    return data_bytes_ == 0 || position_bytes_ >= data_bytes_;
}

bool WavPlayback::readFrames(int16_t* frames, size_t maxFrames, size_t& framesRead)
{
    framesRead = 0;
    if (!frames || maxFrames == 0)
    {
        return false;
    }

    const size_t bytes_to_read = maxFrames * kBytesPerFrame;
    const int    bytes_read    = file().read(frames, bytes_to_read);
    if (bytes_read <= 0)
    {
        return false;
    }

    framesRead = static_cast<size_t>(bytes_read) / kBytesPerFrame;
    return framesRead > 0;
}

bool WavPlayback::seekDataPosition(uint64_t positionBytes)
{
    positionBytes                = clampDataPosition(positionBytes);
    const uint64_t file_position = static_cast<uint64_t>(kWavHeaderBytes) + positionBytes;
    if (!file().seekSet(file_position))
    {
        setError("seek failed");
        return false;
    }

    position_bytes_ = positionBytes;
    resetChannelActivity();
    return true;
}

uint64_t WavPlayback::clampDataPosition(uint64_t positionBytes) const
{
    if (positionBytes > data_bytes_)
    {
        return data_bytes_;
    }

    return positionBytes - (positionBytes % kBytesPerFrame);
}

uint64_t WavPlayback::bytesForMs(uint32_t positionMs) const
{
    const uint64_t frames = (static_cast<uint64_t>(positionMs) * kSampleRateHz) / 1000ULL;
    return clampDataPosition(frames * kBytesPerFrame);
}

uint32_t WavPlayback::msForBytes(uint64_t bytes) const
{
    bytes = clampDataPosition(bytes);
    return static_cast<uint32_t>((bytes * 1000ULL) / kBytesPerSecond);
}

namespace
{

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

uint32_t read_le32(const uint8_t* data)
{
    return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

bool wav_header_valid(const uint8_t* header)
{
    return header && memcmp(header + 0, "RIFF", 4) == 0 && memcmp(header + 8, "WAVE", 4) == 0 &&
           memcmp(header + 12, "fmt ", 4) == 0 && memcmp(header + 36, "data", 4) == 0;
}

} // namespace
