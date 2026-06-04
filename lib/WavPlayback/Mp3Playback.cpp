// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "Mp3Playback.h"

#include <algorithm>
#include <string.h>

namespace
{

enum class Mp3HeaderParseResult
{
    Invalid,
    Unsupported,
    Valid,
};

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

Mp3Playback* g_decode_target = nullptr;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

void                 mp3_data_callback(MP3FrameInfo& info, short* pcm, size_t sample_count);
uint32_t             id3v2_size(const uint8_t* header);
Mp3HeaderParseResult parse_mp3_header(uint32_t  header,
                                      uint32_t& bitrate_kbps,
                                      uint32_t& sample_rate_hz,
                                      uint32_t& frame_size_bytes,
                                      uint16_t& samples_per_frame);

} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

Mp3Playback::Mp3Playback()
{
    decoder_.setDataCallback(mp3_data_callback);
    decoder_.setDelay(0);
}

Mp3Playback::~Mp3Playback()
{
    if (g_decode_target == this)
    {
        g_decode_target = nullptr;
    }
}

void Mp3Playback::handleDecodedFrame(MP3FrameInfo& info, short* pcm, size_t sampleCount)
{
    if (!pcm || sampleCount == 0 || info.nChans <= 0)
    {
        return;
    }

    const uint8_t channels = static_cast<uint8_t>(info.nChans);
    const size_t  frames   = sampleCount / channels;
    if (frames == 0)
    {
        return;
    }

    const uint32_t sample_rate = info.samprate > 0 ? static_cast<uint32_t>(info.samprate) : sample_rate_hz_;
    if (sample_rate > 0)
    {
        sample_rate_hz_ = sample_rate;
    }

    const size_t queued = queuePcmFrames(pcm, frames, channels, sample_rate_hz_);
    decoded_frames_ += queued;
}

const char* Mp3Playback::tag() const
{
    return "Mp3Playback";
}

bool Mp3Playback::beginSource()
{
    file_size_               = file().fileSize();
    encoded_position_bytes_  = 0;
    decoded_frames_          = 0;
    final_decoder_sync_sent_ = false;

    if (!parseMetadata())
    {
        return false;
    }

    return resetDecoder() && seekEncodedBytePosition(0);
}

void Mp3Playback::endSource()
{
    if (g_decode_target == this)
    {
        g_decode_target = nullptr;
    }
    decoder_.end();
}

bool Mp3Playback::seekToTimeMs(uint32_t positionMs)
{
    uint64_t encoded_position = bytesForMs(positionMs);
    if (frame_size_bytes_ > 0)
    {
        encoded_position -= encoded_position % frame_size_bytes_;
    }
    encoded_position = clampEncodedPosition(encoded_position);

    if (!resetDecoder() || !seekEncodedBytePosition(encoded_position))
    {
        return false;
    }

    encoded_position_bytes_  = encoded_position;
    decoded_frames_          = (static_cast<uint64_t>(msForBytes(encoded_position)) * sample_rate_hz_) / 1000ULL;
    final_decoder_sync_sent_ = false;
    resetChannelActivity();
    return true;
}

bool Mp3Playback::pumpSource(size_t maxFrames)
{
    size_t frames_remaining_this_pump = maxFrames;
    while (frames_remaining_this_pump > 0 && !eofMarked())
    {
        if (encoded_position_bytes_ >= encoded_data_bytes_)
        {
            if (!final_decoder_sync_sent_)
            {
                if (framesAvailableToQueue() < kDecodedFrameCapacity)
                {
                    break;
                }

                const uint64_t frames_before = decoded_frames_;
                flushDecoderFinalFrame();
                const uint64_t decoded_delta = decoded_frames_ - frames_before;
                if (decoded_delta >= frames_remaining_this_pump)
                {
                    break;
                }
                frames_remaining_this_pump -= static_cast<size_t>(decoded_delta);
            }

            markEof();
            break;
        }

        if (framesAvailableToQueue() < kDecodedFrameCapacity)
        {
            break;
        }

        size_t       bytes_read    = 0;
        const size_t bytes_to_read = static_cast<size_t>(
            std::min<uint64_t>(sizeof(encoded_buffer_), encoded_data_bytes_ - encoded_position_bytes_));
        if (bytes_to_read == 0 || !readEncodedBytes(encoded_buffer_, bytes_to_read, bytes_read) || bytes_read == 0)
        {
            markEof();
            break;
        }

        encoded_position_bytes_ += bytes_read;

        const uint64_t frames_before = decoded_frames_;
        g_decode_target              = this;
        decoder_.write(encoded_buffer_, bytes_read);
        if (g_decode_target == this)
        {
            g_decode_target = nullptr;
        }

        const uint64_t decoded_delta = decoded_frames_ - frames_before;
        if (decoded_delta >= frames_remaining_this_pump)
        {
            break;
        }
        frames_remaining_this_pump -= static_cast<size_t>(decoded_delta);

        if (encoded_position_bytes_ >= encoded_data_bytes_)
        {
            if (framesAvailableToQueue() < kDecodedFrameCapacity)
            {
                break;
            }

            const uint64_t final_frames_before = decoded_frames_;
            flushDecoderFinalFrame();
            const uint64_t final_decoded_delta = decoded_frames_ - final_frames_before;
            if (final_decoded_delta >= frames_remaining_this_pump)
            {
                break;
            }
            frames_remaining_this_pump -= static_cast<size_t>(final_decoded_delta);

            markEof();
            break;
        }
    }

    return true;
}

uint32_t Mp3Playback::sourceDurationMs() const
{
    return duration_ms_;
}

uint32_t Mp3Playback::sourcePositionMs() const
{
    if (sample_rate_hz_ == 0)
    {
        return 0;
    }

    const uint32_t position = static_cast<uint32_t>((decoded_frames_ * 1000ULL) / sample_rate_hz_);
    return position > duration_ms_ ? duration_ms_ : position;
}

bool Mp3Playback::sourceAtEnd() const
{
    return duration_ms_ == 0 || (encoded_position_bytes_ >= encoded_data_bytes_ && final_decoder_sync_sent_) ||
           sourcePositionMs() >= duration_ms_;
}

uint32_t Mp3Playback::speakerSampleRateHz() const
{
    return sample_rate_hz_ == 0 ? kSampleRateHz : sample_rate_hz_;
}

bool Mp3Playback::parseMetadata()
{
    static constexpr size_t   kScanBufferBytes        = 2048;
    static constexpr uint64_t kMaxMp3HeaderSearchBytes = 64ULL * 1024ULL;

    uint8_t scan[kScanBufferBytes] = {};
    if (!file().seekSet(0))
    {
        setError("seek failed");
        return false;
    }

    const int bytes_read = file().read(scan, 10);
    if (bytes_read < 4)
    {
        setError("MP3 header read failed");
        return false;
    }

    uint64_t search_offset = 0;
    if (bytes_read >= 10 && memcmp(scan, "ID3", 3) == 0)
    {
        search_offset = 10ULL + id3v2_size(scan);
        if ((scan[5] & 0x10) != 0)
        {
            search_offset += 10ULL;
        }
    }

    bool     found                  = false;
    uint16_t samples_per_frame      = MP3_PCM_FRAMES_PER_MP3_FRAME;
    uint64_t scanned_bytes          = 0;
    bool     saw_unsupported_header = false;
    while (search_offset < file_size_ && scanned_bytes < kMaxMp3HeaderSearchBytes)
    {
        if (!file().seekSet(search_offset))
        {
            setError("seek failed");
            return false;
        }

        const uint64_t bytes_left       = file_size_ - search_offset;
        const uint64_t search_left      = kMaxMp3HeaderSearchBytes - scanned_bytes;
        const size_t   bytes_to_read =
            static_cast<size_t>(std::min<uint64_t>(sizeof(scan), std::min(bytes_left, search_left)));
        const int scan_bytes_read = file().read(scan, bytes_to_read);
        if (scan_bytes_read < 4)
        {
            break;
        }

        const size_t scan_size = static_cast<size_t>(scan_bytes_read);
        for (size_t i = 0; i + 4 <= scan_size; ++i)
        {
            const uint32_t header = (static_cast<uint32_t>(scan[i]) << 24) |
                                    (static_cast<uint32_t>(scan[i + 1]) << 16) |
                                    (static_cast<uint32_t>(scan[i + 2]) << 8) | static_cast<uint32_t>(scan[i + 3]);
            uint32_t candidate_bitrate_kbps      = 0;
            uint32_t candidate_sample_rate_hz    = 0;
            uint32_t candidate_frame_size_bytes  = 0;
            uint16_t candidate_samples_per_frame = 0;
            const Mp3HeaderParseResult parse_result = parse_mp3_header(header,
                                                                       candidate_bitrate_kbps,
                                                                       candidate_sample_rate_hz,
                                                                       candidate_frame_size_bytes,
                                                                       candidate_samples_per_frame);
            if (parse_result == Mp3HeaderParseResult::Unsupported)
            {
                saw_unsupported_header = true;
            }
            if (parse_result != Mp3HeaderParseResult::Valid)
            {
                continue;
            }

            uint32_t next_bitrate_kbps      = 0;
            uint32_t next_sample_rate_hz    = 0;
            uint32_t next_frame_size_bytes  = 0;
            uint16_t next_samples_per_frame = 0;
            const uint64_t candidate_offset  = search_offset + i;
            const uint64_t next_frame_offset = candidate_offset + candidate_frame_size_bytes;
            uint8_t        next_header_bytes[4]  = {};
            bool           next_frame_valid      = next_frame_offset >= file_size_;
            if (!next_frame_valid && file().seekSet(next_frame_offset) &&
                file().read(next_header_bytes, sizeof(next_header_bytes)) == static_cast<int>(sizeof(next_header_bytes)))
            {
                const uint32_t next_header = (static_cast<uint32_t>(next_header_bytes[0]) << 24) |
                                             (static_cast<uint32_t>(next_header_bytes[1]) << 16) |
                                             (static_cast<uint32_t>(next_header_bytes[2]) << 8) |
                                             static_cast<uint32_t>(next_header_bytes[3]);
                const Mp3HeaderParseResult next_parse_result = parse_mp3_header(next_header,
                                                                                next_bitrate_kbps,
                                                                                next_sample_rate_hz,
                                                                                next_frame_size_bytes,
                                                                                next_samples_per_frame);
                if (next_parse_result == Mp3HeaderParseResult::Unsupported)
                {
                    saw_unsupported_header = true;
                }
                next_frame_valid = next_parse_result == Mp3HeaderParseResult::Valid &&
                                   next_sample_rate_hz == candidate_sample_rate_hz &&
                                   next_samples_per_frame == candidate_samples_per_frame;
            }
            if (next_frame_valid)
            {
                bitrate_kbps_      = candidate_bitrate_kbps;
                sample_rate_hz_    = candidate_sample_rate_hz;
                frame_size_bytes_  = candidate_frame_size_bytes;
                samples_per_frame  = candidate_samples_per_frame;
                data_start_offset_ = candidate_offset;
                found              = true;
                break;
            }
        }
        if (found)
        {
            break;
        }

        const size_t advance = scan_size > 3 ? scan_size - 3 : scan_size;
        search_offset += advance;
        scanned_bytes += advance;
    }

    if (!found)
    {
        setError("bad MP3 header");
        return false;
    }

    if (file_size_ <= data_start_offset_)
    {
        setError("empty MP3");
        return false;
    }

    encoded_data_bytes_ = file_size_ - data_start_offset_;
    bytes_per_second_   = (bitrate_kbps_ * 1000UL) / 8UL;
    if (bytes_per_second_ == 0)
    {
        setError("bad MP3 bitrate");
        return false;
    }

    duration_ms_        = static_cast<uint32_t>((encoded_data_bytes_ * 1000ULL) / bytes_per_second_);
    if (saw_unsupported_header || sample_rate_hz_ != AUDIO_RECORDER_SAMPLE_RATE_HZ || bitrate_kbps_ != MP3_BITRATE_KBPS)
    {
        setWarning("unsupported format");
    }
    (void)samples_per_frame;
    return true;
}

bool Mp3Playback::readEncodedBytes(uint8_t* data, size_t maxBytes, size_t& bytesRead)
{
    bytesRead = 0;
    if (!data || maxBytes == 0)
    {
        return false;
    }

    const int result = file().read(data, maxBytes);
    if (result <= 0)
    {
        return false;
    }

    bytesRead = static_cast<size_t>(result);
    return true;
}

bool Mp3Playback::seekEncodedBytePosition(uint64_t positionBytes)
{
    positionBytes = clampEncodedPosition(positionBytes);
    if (!file().seekSet(data_start_offset_ + positionBytes))
    {
        setError("seek failed");
        return false;
    }

    encoded_position_bytes_ = positionBytes;
    return true;
}

uint64_t Mp3Playback::clampEncodedPosition(uint64_t positionBytes) const
{
    return positionBytes > encoded_data_bytes_ ? encoded_data_bytes_ : positionBytes;
}

uint64_t Mp3Playback::bytesForMs(uint32_t positionMs) const
{
    if (bytes_per_second_ == 0)
    {
        return 0;
    }

    return clampEncodedPosition((static_cast<uint64_t>(positionMs) * bytes_per_second_) / 1000ULL);
}

uint32_t Mp3Playback::msForBytes(uint64_t bytes) const
{
    if (bytes_per_second_ == 0)
    {
        return 0;
    }

    bytes = clampEncodedPosition(bytes);
    return static_cast<uint32_t>((bytes * 1000ULL) / bytes_per_second_);
}

bool Mp3Playback::resetDecoder()
{
    if (g_decode_target == this)
    {
        g_decode_target = nullptr;
    }
    decoder_.end();
    decoder_.setDataCallback(mp3_data_callback);
    decoder_.setDelay(0);
    decoder_.begin();
    final_decoder_sync_sent_ = false;
    return static_cast<bool>(decoder_);
}

void Mp3Playback::flushDecoderFinalFrame()
{
    static constexpr uint8_t kFinalSyncProbe[] = {0xFF, 0xF0, 0x00, 0x00};

    if (final_decoder_sync_sent_)
    {
        return;
    }

    g_decode_target = this;
    decoder_.write(kFinalSyncProbe, sizeof(kFinalSyncProbe));
    if (g_decode_target == this)
    {
        g_decode_target = nullptr;
    }
    final_decoder_sync_sent_ = true;
}

namespace
{

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

void mp3_data_callback(MP3FrameInfo& info, short* pcm, size_t sample_count)
{
    if (g_decode_target)
    {
        g_decode_target->handleDecodedFrame(info, pcm, sample_count);
    }
}

uint32_t id3v2_size(const uint8_t* header)
{
    return (static_cast<uint32_t>(header[6] & 0x7F) << 21) | (static_cast<uint32_t>(header[7] & 0x7F) << 14) |
           (static_cast<uint32_t>(header[8] & 0x7F) << 7) | static_cast<uint32_t>(header[9] & 0x7F);
}

Mp3HeaderParseResult parse_mp3_header(uint32_t  header,
                                      uint32_t& bitrate_kbps,
                                      uint32_t& sample_rate_hz,
                                      uint32_t& frame_size_bytes,
                                      uint16_t& samples_per_frame)
{
    if ((header & 0xFFE00000UL) != 0xFFE00000UL)
    {
        return Mp3HeaderParseResult::Invalid;
    }

    // MPEG audio frame header layout:
    // version: 3=MPEG-1, 2=MPEG-2, 0=MPEG-2.5, 1=reserved/invalid.
    // layer:   1=Layer III. This decoder path is MP3-only, so Layer I/II are rejected.
    const uint8_t version           = static_cast<uint8_t>((header >> 19) & 0x03);
    const uint8_t layer             = static_cast<uint8_t>((header >> 17) & 0x03);
    const uint8_t bitrate_index     = static_cast<uint8_t>((header >> 12) & 0x0F);
    const uint8_t sample_rate_index = static_cast<uint8_t>((header >> 10) & 0x03);
    const uint8_t padding           = static_cast<uint8_t>((header >> 9) & 0x01);

    // Version 1 and layer 0 are reserved; bitrate index 15 is forbidden; sample-rate index 3 is reserved.
    if (version == 1 || layer == 0 || bitrate_index == 15 || sample_rate_index == 3)
    {
        return Mp3HeaderParseResult::Invalid;
    }

    // Layer I/II and free-format bitrate are valid MPEG audio cases, but this player
    // cannot estimate frame boundaries/duration for them.
    if ((layer == 2 || layer == 3) || bitrate_index == 0)
    {
        return Mp3HeaderParseResult::Unsupported;
    }

    // Layer III has different bitrate tables for MPEG-1 vs MPEG-2/2.5.
    // The header indices are 1..14 after the rejection above; slot 0 is kept only to match the spec index values.
    static constexpr uint16_t kMpeg1Bitrates[]     = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
    static constexpr uint16_t kMpeg2Bitrates[]     = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
    static constexpr uint32_t kMpeg1SampleRates[]  = {44100, 48000, 32000};
    static constexpr uint32_t kMpeg2SampleRates[]  = {22050, 24000, 16000};
    static constexpr uint32_t kMpeg25SampleRates[] = {11025, 12000, 8000};

    if (version == 3)
    {
        bitrate_kbps      = kMpeg1Bitrates[bitrate_index];
        sample_rate_hz    = kMpeg1SampleRates[sample_rate_index];
        samples_per_frame = 1152;
        // MPEG-1 Layer III frame length: floor(144 * bitrate / sample_rate) + padding.
        // bitrate is stored as kbps, so 144 * 1000 becomes 144000.
        frame_size_bytes  = ((144000UL * bitrate_kbps) / sample_rate_hz) + padding;
    }
    else
    {
        bitrate_kbps      = kMpeg2Bitrates[bitrate_index];
        sample_rate_hz    = version == 2 ? kMpeg2SampleRates[sample_rate_index] : kMpeg25SampleRates[sample_rate_index];
        samples_per_frame = 576;
        // MPEG-2/2.5 Layer III uses half the MPEG-1 samples per frame, so the frame-length
        // coefficient is 72 instead of 144.
        frame_size_bytes  = ((72000UL * bitrate_kbps) / sample_rate_hz) + padding;
    }

    return bitrate_kbps > 0 && sample_rate_hz > 0 && frame_size_bytes > 0 ? Mp3HeaderParseResult::Valid
                                                                           : Mp3HeaderParseResult::Invalid;
}

} // namespace
