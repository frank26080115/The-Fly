#include "Mp3EncryptedPlayback.h"

#include <algorithm>
#include <string.h>

#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
#include "AudioFileRecorder.h"
#endif

const char* Mp3EncryptedPlayback::tag() const
{
    return "Mp3EncryptedPlayback";
}

bool Mp3EncryptedPlayback::parseMetadata()
{
#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
    file_size_ = file().fileSize();
    data_start_offset_ = 0;
    encoded_position_bytes_ = 0;
    encoded_data_bytes_ = encryptedPayloadBytesForFileSize(file_size_);
    decoded_frames_ = 0;
    sample_rate_hz_ = AUDIO_RECORDER_SAMPLE_RATE_HZ;
    bitrate_kbps_ = MP3_BITRATE_KBPS;
    bytes_per_second_ = MP3_CBR_BYTES_PER_SECOND;
    frame_size_bytes_ = MP3_CBR_BYTES_PER_MP3_FRAME;
    duration_ms_ = static_cast<uint32_t>((encoded_data_bytes_ * 1000ULL) / bytes_per_second_);
    resetLoadedChunk();

    return beginDecryption();
#else
    setError("encrypted playback unavailable");
    return false;
#endif
}

void Mp3EncryptedPlayback::endSource()
{
    Mp3Playback::endSource();
#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
    endDecryption();
#endif
    resetLoadedChunk();
}

bool Mp3EncryptedPlayback::readEncodedBytes(uint8_t* data, size_t maxBytes, size_t& bytesRead)
{
    bytesRead = 0;
#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
    if (!data || maxBytes == 0)
    {
        return false;
    }

    size_t copied = 0;
    while (copied < maxBytes && encoded_position_bytes_ + copied < encoded_data_bytes_)
    {
        const uint64_t byte_position = encoded_position_bytes_ + copied;
        if (!ensureChunk(byte_position))
        {
            return copied > 0;
        }

        uint8_t* plaintext = AudioFileRecorder::wavPlaintextAudioBuffer();
        if (!plaintext)
        {
            setError("plain buffer missing");
            return copied > 0;
        }

        const size_t chunk_offset = static_cast<size_t>(byte_position % MP3_ENCRYPTED_PLAINTEXT_LENGTH);
        const size_t chunk_remaining = static_cast<size_t>(
            std::min<uint64_t>(MP3_ENCRYPTED_PLAINTEXT_LENGTH - chunk_offset, encoded_data_bytes_ - byte_position));
        const size_t copy_bytes = std::min(maxBytes - copied, chunk_remaining);
        if (copy_bytes == 0)
        {
            break;
        }

        memcpy(data + copied, plaintext + chunk_offset, copy_bytes);
        copied += copy_bytes;
    }

    bytesRead = copied;
    return bytesRead > 0;
#else
    (void)data;
    (void)maxBytes;
    setError("encrypted playback unavailable");
    return false;
#endif
}

bool Mp3EncryptedPlayback::seekEncodedBytePosition(uint64_t positionBytes)
{
    encoded_position_bytes_ = clampEncodedPosition(positionBytes);
    resetLoadedChunk();
    return true;
}

uint64_t Mp3EncryptedPlayback::encryptedPayloadBytesForFileSize(uint64_t fileSize) const
{
    const uint64_t full_chunks = fileSize / MP3_ENCRYPTED_CHUNK_LENGTH;
    const uint64_t remainder = fileSize % MP3_ENCRYPTED_CHUNK_LENGTH;
    uint64_t payload_bytes = full_chunks * MP3_ENCRYPTED_PLAINTEXT_LENGTH;

    if (remainder > RECORDER_ENCRYPTED_CHUNK_OVERHEAD)
    {
        payload_bytes += remainder - RECORDER_ENCRYPTED_CHUNK_OVERHEAD;
    }

    return payload_bytes;
}

bool Mp3EncryptedPlayback::ensureChunk(uint64_t positionBytes)
{
#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
    const uint64_t chunk_index = positionBytes / MP3_ENCRYPTED_PLAINTEXT_LENGTH;
    if (loaded_encrypted_chunk_ == chunk_index)
    {
        return true;
    }

    return readEncryptedChunk(chunk_index);
#else
    (void)positionBytes;
    setError("encrypted playback unavailable");
    return false;
#endif
}

bool Mp3EncryptedPlayback::readEncryptedChunk(uint64_t chunkIndex)
{
#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
    uint8_t* encrypted = AudioFileRecorder::wavEncryptedAudioBuffer();
    uint8_t* plaintext = AudioFileRecorder::wavPlaintextAudioBuffer();
    if (!encrypted || !plaintext)
    {
        setError("decrypt buffer missing");
        return false;
    }
    if (AudioFileRecorder::wavEncryptedAudioBufferSize() < MP3_ENCRYPTED_CHUNK_LENGTH ||
        AudioFileRecorder::wavPlaintextAudioBufferSize() < MP3_ENCRYPTED_PLAINTEXT_LENGTH)
    {
        setError("decrypt buffer small");
        return false;
    }

    const uint64_t file_position = chunkIndex * static_cast<uint64_t>(MP3_ENCRYPTED_CHUNK_LENGTH);
    if (!file().seekSet(file_position))
    {
        setError("seek failed");
        return false;
    }

    const int bytes_read = file().read(encrypted, MP3_ENCRYPTED_CHUNK_LENGTH);
    if (bytes_read != static_cast<int>(MP3_ENCRYPTED_CHUNK_LENGTH))
    {
        setError("read failed");
        return false;
    }

    if (!decryptChunk(encrypted, MP3_ENCRYPTED_PLAINTEXT_LENGTH, plaintext))
    {
        resetLoadedChunk();
        return false;
    }

    loaded_encrypted_chunk_ = chunkIndex;
    return true;
#else
    (void)chunkIndex;
    setError("encrypted playback unavailable");
    return false;
#endif
}

void Mp3EncryptedPlayback::resetLoadedChunk()
{
    loaded_encrypted_chunk_ = kNoEncryptedChunk;
}
