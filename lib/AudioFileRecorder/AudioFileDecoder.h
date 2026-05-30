#pragma once

#include "thefly_common.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class AudioFifo;

namespace AudioFileRecorder
{

class AudioFileDecoder
{
public:
    static constexpr size_t kPathMaxLength = 192;

    enum class State
    {
        Idle,
        Busy,
        Done,
        Error,
        Cancelled,
    };

    enum class Error
    {
        None,
        AlreadyBusy,
        InvalidArgument,
        SdNotReady,
        TaskCreateFailed,
        AllocationFailed,
        InputOpenFailed,
        InputReadFailed,
        OutputOpenFailed,
        OutputWriteFailed,
        OutputSeekFailed,
        OutputRenameFailed,
        InvalidPacket,
        EncryptionSetupFailed,
        DecryptionFailed,
        Cancelled,
    };

    struct Status
    {
        State    state           = State::Idle;
        Error    error           = Error::None;
        uint64_t bytes_processed = 0;
        uint64_t bytes_total     = 0;
        float    progress        = 0.0f;
        bool     finished        = false;
        char     input_path[kPathMaxLength] = {};
        char     output_path[kPathMaxLength] = {};
    };

    using CompleteCallback = void (*)(const Status& status);
    using ProgressCallback = void (*)(const Status& status);

    AudioFileDecoder(const char* inputPath,
                     CompleteCallback onComplete = nullptr,
                     ProgressCallback onProgress = nullptr);
    ~AudioFileDecoder();

    AudioFileDecoder(const AudioFileDecoder&)            = delete;
    AudioFileDecoder& operator=(const AudioFileDecoder&) = delete;

    bool start();
    bool cancel();
    void poll();

    State  state() const;
    Error  error() const;
    Status status() const;
    float  progress() const;

    void setOnCompleteCallback(CompleteCallback callback);
    void setOnProgressCallback(ProgressCallback callback);

    const char* stateName() const;
    const char* errorName() const;

private:
    static void taskEntry(void* argument);

    bool allocateWorkingBuffers();
    void releaseWorkingBuffers();
    bool startTaskOnCurrentCore();
    void taskMain();
    void finish(State state, Error error);
    void setFinishedWithoutTask(State state, Error error);
    void updateProgress(uint64_t bytesProcessed, uint64_t bytesTotal, bool notify);
    bool cancelRequested() const;

    mutable std::mutex m_mutex;
    TaskHandle_t       m_task_handle = nullptr;
    Status             m_status = {};
    CompleteCallback   m_on_complete = nullptr;
    ProgressCallback   m_on_progress = nullptr;
    bool               m_complete_callback_fired = false;
    bool               m_progress_pending = false;
    std::atomic<bool>  m_cancel_requested { false };

    std::unique_ptr<AudioFifo>       m_left_fifo;
    std::unique_ptr<AudioFifo>       m_right_fifo;
    std::unique_ptr<file_packet_t>   m_packet;
    std::unique_ptr<uint8_t[]>       m_encrypted_packet;
    std::unique_ptr<int16_t[]>       m_packet_samples;
    std::unique_ptr<int16_t[]>       m_left_samples;
    std::unique_ptr<int16_t[]>       m_right_samples;
    std::unique_ptr<int16_t[]>       m_stereo_samples;
};

} // namespace AudioFileRecorder
