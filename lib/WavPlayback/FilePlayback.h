#pragma once

#include "thefly_common.h"

#include <Arduino.h>
#include <SdFat.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

#include "AudioFifo.h"

#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
#include "mbedtls/gcm.h"
#endif

class FilePlayback
{
public:
    static std::unique_ptr<FilePlayback> createForPath(const char* path);

    virtual ~FilePlayback();

    bool start(const char* path);
    void stop();
    void pump();

    bool active() const;
    bool playing() const;
    bool paused() const;
    bool finished() const;

    void setPlaying(bool playing);
    void togglePlaying();
    void setPositionMs(uint32_t positionMs);
    void setVolume(uint8_t volume);

    uint32_t    durationMs() const;
    uint32_t    positionMs() const;
    uint8_t     volume() const;
    const char* path() const;
    const char* lastError() const;
    const char* lastWarning() const;

protected:
    static constexpr uint32_t kSampleRateHz            = 16000;
    static constexpr uint32_t kPumpTargetFrames        = kSampleRateHz / 2;
    static constexpr size_t   kSpeakerWatermarkSamples = 240;
    static constexpr size_t   kInactiveZeroRunFrames   = 8;
    static constexpr size_t   kMonoQueueBufferFrames   = 1152;

    FilePlayback() = default;

    virtual const char* tag() const   = 0;
    virtual bool        beginSource() = 0;
    virtual void        endSource();
    virtual bool        seekToTimeMs(uint32_t positionMs) = 0;
    virtual bool        pumpSource(size_t maxFrames)      = 0;
    virtual uint32_t    sourceDurationMs() const          = 0;
    virtual uint32_t    sourcePositionMs() const          = 0;
    virtual bool        sourceAtEnd() const               = 0;
    virtual uint32_t    speakerSampleRateHz() const;

    FsFile&       file();
    const FsFile& file() const;

    void    setError(const char* error);
    void    setWarning(const char* warning);
    void    markEof();
    bool    eofMarked() const;
    void    resetChannelActivity();
    size_t  framesAvailableToQueue();
    size_t  queueMonoSamples(const int16_t* samples, size_t frames, uint32_t sampleRateHz = kSampleRateHz);
    size_t  queuePcmFrames(const int16_t* samples, size_t frames, uint8_t channels, uint32_t sampleRateHz);
    int16_t mixStereoFrameToMono(const int16_t* frame);

#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
    bool beginDecryption();
    void endDecryption();
    bool decryptChunk(const uint8_t* encrypted, size_t plaintextSize, uint8_t* plaintext);
#endif

private:
    void     finish();
    void     clearFifoAndMaybeRewind(bool rewindQueuedAudio);
    bool     setupSpeaker();
    void     closeFileAndSource();
    uint32_t clampedSourcePositionMs() const;

    mutable std::mutex mutex_;
    FsFile             file_;
    bool               active_                              = false;
    bool               playing_                             = false;
    bool               finished_                            = false;
    bool               eof_                                 = false;
    uint8_t            volume_                              = 40;
    char               path_[96]                            = {};
    char               error_[96]                           = {};
    char               warning_[64]                         = {};
    size_t             left_zero_run_                       = kInactiveZeroRunFrames;
    size_t             right_zero_run_                      = kInactiveZeroRunFrames;
    int16_t            mono_buffer_[kMonoQueueBufferFrames] = {};

#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
    mbedtls_gcm_context playback_gcm_;
    bool                playback_gcm_ready_ = false;
#endif
};
