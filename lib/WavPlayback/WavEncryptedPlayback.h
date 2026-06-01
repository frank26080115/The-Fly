#pragma once

#include "WavPlayback.h"

#include <cstddef>
#include <cstdint>

class WavEncryptedPlayback : public WavPlayback
{
public:
    WavEncryptedPlayback() = default;
    ~WavEncryptedPlayback() override = default;

protected:
    const char* tag() const override;
    bool beginSource() override;
    void endSource() override;
    bool readFrames(int16_t* frames, size_t maxFrames, size_t& framesRead) override;
    bool seekDataPosition(uint64_t positionBytes) override;

private:
    static constexpr uint64_t kNoEncryptedChunk = UINT64_MAX;

    uint64_t encryptedDataBytesForFileSize(uint64_t fileSize) const;
    bool readEncryptedBlock(uint64_t filePosition, size_t plaintextSize);
    bool decryptEncryptedHeader();
    bool ensureAudioChunk(uint64_t positionBytes);
    void resetLoadedChunk();

    uint64_t loaded_encrypted_chunk_ = kNoEncryptedChunk;
};
