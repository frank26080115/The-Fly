#pragma once

#include "FilePlayback.h"

#include <cstddef>
#include <cstdint>

#include "MP3DecoderHelix.h"

class Mp3Playback : public FilePlayback
{
public:
    Mp3Playback();
    ~Mp3Playback() override;

    void handleDecodedFrame(MP3FrameInfo& info, short* pcm, size_t sampleCount);

protected:
    static constexpr size_t kEncodedReadBytes     = 576;
    static constexpr size_t kDecodedFrameCapacity = 1152;

    const char* tag() const override;
    bool        beginSource() override;
    void        endSource() override;
    bool        seekToTimeMs(uint32_t positionMs) override;
    bool        pumpSource(size_t maxFrames) override;
    uint32_t    sourceDurationMs() const override;
    uint32_t    sourcePositionMs() const override;
    bool        sourceAtEnd() const override;

    virtual bool parseMetadata();
    virtual bool readEncodedBytes(uint8_t* data, size_t maxBytes, size_t& bytesRead);
    virtual bool seekEncodedBytePosition(uint64_t positionBytes);

    uint64_t clampEncodedPosition(uint64_t positionBytes) const;
    uint64_t bytesForMs(uint32_t positionMs) const;
    uint32_t msForBytes(uint64_t bytes) const;
    bool     resetDecoder();
    void     flushDecoderFinalFrame();

    libhelix::MP3DecoderHelix decoder_;
    uint8_t                   encoded_buffer_[kEncodedReadBytes] = {};
    uint64_t                  file_size_                         = 0;
    uint64_t                  data_start_offset_                 = 0;
    uint64_t                  encoded_data_bytes_                = 0;
    uint64_t                  encoded_position_bytes_            = 0;
    uint64_t                  decoded_frames_                    = 0;
    uint32_t                  duration_ms_                       = 0;
    uint32_t                  sample_rate_hz_                    = AUDIO_RECORDER_SAMPLE_RATE_HZ;
    uint32_t                  bitrate_kbps_                      = MP3_BITRATE_KBPS;
    uint32_t                  bytes_per_second_                  = MP3_CBR_BYTES_PER_SECOND;
    uint32_t                  frame_size_bytes_                  = MP3_CBR_BYTES_PER_MP3_FRAME;
    bool                      final_decoder_sync_sent_           = false;
};
