#pragma once

#include "FilePlayback.h"

#include <cstddef>
#include <cstdint>

class WavPlayback : public FilePlayback
{
public:
    static constexpr uint32_t kWavHeaderBytes = WAV_RIFF_HEADER_LENGTH;

    WavPlayback()           = default;
    ~WavPlayback() override = default;

protected:
    static constexpr uint32_t kChannels        = 2;
    static constexpr uint32_t kBytesPerSample  = sizeof(int16_t);
    static constexpr uint32_t kBytesPerFrame   = kChannels * kBytesPerSample;
    static constexpr uint32_t kBytesPerSecond  = kSampleRateHz * kBytesPerFrame;
    static constexpr size_t   kReadChunkFrames = 512;

    const char* tag() const override;
    bool        beginSource() override;
    bool        seekToTimeMs(uint32_t positionMs) override;
    bool        pumpSource(size_t maxFrames) override;
    uint32_t    sourceDurationMs() const override;
    uint32_t    sourcePositionMs() const override;
    bool        sourceAtEnd() const override;

    virtual bool readFrames(int16_t* frames, size_t maxFrames, size_t& framesRead);
    virtual bool seekDataPosition(uint64_t positionBytes);
    uint64_t     clampDataPosition(uint64_t positionBytes) const;
    uint64_t     bytesForMs(uint32_t positionMs) const;
    uint32_t     msForBytes(uint64_t bytes) const;

    uint64_t file_size_                                 = 0;
    uint64_t data_bytes_                                = 0;
    uint64_t position_bytes_                            = 0;
    int16_t  read_buffer_[kReadChunkFrames * kChannels] = {};
};
