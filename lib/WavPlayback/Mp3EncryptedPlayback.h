#pragma once

#include "Mp3Playback.h"

#include <cstddef>
#include <cstdint>

class Mp3EncryptedPlayback : public Mp3Playback
{
public:
    Mp3EncryptedPlayback() = default;
    ~Mp3EncryptedPlayback() override = default;

protected:
    const char* tag() const override;
    bool parseMetadata() override;
    void endSource() override;
    bool readEncodedBytes(uint8_t* data, size_t maxBytes, size_t& bytesRead) override;
    bool seekEncodedBytePosition(uint64_t positionBytes) override;

private:
    static constexpr uint64_t kNoEncryptedChunk = UINT64_MAX;

    uint64_t encryptedPayloadBytesForFileSize(uint64_t fileSize) const;
    bool ensureChunk(uint64_t positionBytes);
    bool readEncryptedChunk(uint64_t chunkIndex);
    void resetLoadedChunk();

    uint64_t loaded_encrypted_chunk_ = kNoEncryptedChunk;
};
