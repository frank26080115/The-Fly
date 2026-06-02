// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "WavEncryptedPlayback.h"

#include <algorithm>
#include <string.h>

#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
#include "AudioFileRecorder.h"
#endif

namespace
{

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

bool wav_header_valid(const uint8_t* header);

} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

const char* WavEncryptedPlayback::tag() const
{
    return "WavEncryptedPlayback";
}

bool WavEncryptedPlayback::beginSource()
{
#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
    file_size_      = file().fileSize();
    data_bytes_     = encryptedDataBytesForFileSize(file_size_);
    data_bytes_     = clampDataPosition(data_bytes_);
    position_bytes_ = 0;
    resetLoadedChunk();

    return beginDecryption() && decryptEncryptedHeader() && seekDataPosition(0);
#else
    setError("encrypted playback unavailable");
    return false;
#endif
}

void WavEncryptedPlayback::endSource()
{
#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
    endDecryption();
#endif
    resetLoadedChunk();
}

bool WavEncryptedPlayback::readFrames(int16_t* frames, size_t maxFrames, size_t& framesRead)
{
    framesRead = 0;
#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
    if (!frames || maxFrames == 0)
    {
        return false;
    }

    size_t copied_frames = 0;
    while (copied_frames < maxFrames && position_bytes_ + copied_frames * kBytesPerFrame < data_bytes_)
    {
        const uint64_t byte_position = position_bytes_ + copied_frames * kBytesPerFrame;
        if (!ensureAudioChunk(byte_position))
        {
            return copied_frames > 0;
        }

        uint8_t* plaintext = AudioFileRecorder::wavPlaintextAudioBuffer();
        if (!plaintext)
        {
            setError("plain buffer missing");
            return copied_frames > 0;
        }

        const size_t chunk_offset          = static_cast<size_t>(byte_position % WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH);
        const size_t chunk_bytes_remaining = static_cast<size_t>(
            std::min<uint64_t>(WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH - chunk_offset, data_bytes_ - byte_position));
        const size_t chunk_frames_remaining = chunk_bytes_remaining / kBytesPerFrame;
        const size_t frames_to_copy         = std::min(maxFrames - copied_frames, chunk_frames_remaining);
        if (frames_to_copy == 0)
        {
            break;
        }

        memcpy(frames + copied_frames * kChannels, plaintext + chunk_offset, frames_to_copy * kBytesPerFrame);
        copied_frames += frames_to_copy;
    }

    framesRead = copied_frames;
    return framesRead > 0;
#else
    (void)frames;
    (void)maxFrames;
    setError("encrypted playback unavailable");
    return false;
#endif
}

bool WavEncryptedPlayback::seekDataPosition(uint64_t positionBytes)
{
    position_bytes_ = clampDataPosition(positionBytes);
    resetLoadedChunk();
    resetChannelActivity();
    return true;
}

uint64_t WavEncryptedPlayback::encryptedDataBytesForFileSize(uint64_t fileSize) const
{
    if (fileSize <= WAV_ENCRYPTED_RIFF_HEADER_LENGTH)
    {
        return 0;
    }

    const uint64_t encrypted_audio_bytes = fileSize - WAV_ENCRYPTED_RIFF_HEADER_LENGTH;
    const uint64_t encrypted_chunks      = encrypted_audio_bytes / WAV_ENCRYPTED_AUDIO_CHUNK_LENGTH;
    return encrypted_chunks * WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH;
}

bool WavEncryptedPlayback::readEncryptedBlock(uint64_t filePosition, size_t plaintextSize)
{
#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
    uint8_t* encrypted = AudioFileRecorder::wavEncryptedAudioBuffer();
    uint8_t* plaintext = AudioFileRecorder::wavPlaintextAudioBuffer();
    if (!encrypted || !plaintext)
    {
        setError("decrypt buffer missing");
        return false;
    }
    if (AudioFileRecorder::wavPlaintextAudioBufferSize() < plaintextSize)
    {
        setError("plain buffer small");
        return false;
    }

    const size_t encrypted_size =
        RECORDER_ENCRYPTED_CHUNK_NONCE_LENGTH + plaintextSize + RECORDER_ENCRYPTED_CHUNK_TAG_LENGTH;
    if (AudioFileRecorder::wavEncryptedAudioBufferSize() < encrypted_size)
    {
        setError("enc buffer small");
        return false;
    }
    if (!file().seekSet(filePosition))
    {
        setError("seek failed");
        return false;
    }

    const int bytes_read = file().read(encrypted, encrypted_size);
    if (bytes_read != static_cast<int>(encrypted_size))
    {
        setError("read failed");
        return false;
    }

    return decryptChunk(encrypted, plaintextSize, plaintext);
#else
    (void)filePosition;
    (void)plaintextSize;
    setError("encrypted playback unavailable");
    return false;
#endif
}

bool WavEncryptedPlayback::decryptEncryptedHeader()
{
#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
    if (!readEncryptedBlock(0, WAV_RIFF_HEADER_LENGTH))
    {
        return false;
    }

    if (!wav_header_valid(AudioFileRecorder::wavPlaintextAudioBuffer()))
    {
        setError("bad WAV header");
        return false;
    }

    return true;
#else
    setError("encrypted playback unavailable");
    return false;
#endif
}

bool WavEncryptedPlayback::ensureAudioChunk(uint64_t positionBytes)
{
#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
    const uint64_t chunk_index = positionBytes / WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH;
    if (loaded_encrypted_chunk_ == chunk_index)
    {
        return true;
    }

    const uint64_t file_position = static_cast<uint64_t>(WAV_ENCRYPTED_RIFF_HEADER_LENGTH) +
                                   chunk_index * static_cast<uint64_t>(WAV_ENCRYPTED_AUDIO_CHUNK_LENGTH);
    if (!readEncryptedBlock(file_position, WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH))
    {
        resetLoadedChunk();
        return false;
    }

    loaded_encrypted_chunk_ = chunk_index;
    return true;
#else
    (void)positionBytes;
    setError("encrypted playback unavailable");
    return false;
#endif
}

void WavEncryptedPlayback::resetLoadedChunk()
{
    loaded_encrypted_chunk_ = kNoEncryptedChunk;
}

namespace
{

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

bool wav_header_valid(const uint8_t* header)
{
    return header && memcmp(header + 0, "RIFF", 4) == 0 && memcmp(header + 8, "WAVE", 4) == 0 &&
           memcmp(header + 12, "fmt ", 4) == 0 && memcmp(header + 36, "data", 4) == 0;
}

} // namespace
