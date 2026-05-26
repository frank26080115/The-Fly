#pragma once

#include <stddef.h>
#include <stdint.h>

#include "WifiManager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class CloudUpload
{
public:
    static constexpr uint8_t  kMaxRetries        = 3;
    static constexpr size_t   kPathMaxLength     = 192;
    static constexpr size_t   kNameMaxLength     = 64;
    static constexpr size_t   kUrlMaxLength      = 256;
    static constexpr size_t   kHostMaxLength     = 128;
    static constexpr size_t   kMessageMaxLength  = 160;
    static constexpr uint32_t kDefaultTimeoutMs  = 30000;

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
        WifiNotConnected,
        TaskCreateFailed,
        AllocationFailed,
        HistoryOpenFailed,
        ScanFailed,
        FileOpenFailed,
        NetworkConnectFailed,
        NetworkWriteFailed,
        HttpError,
        JsonParseFailed,
        ServerError,
        HistoryWriteFailed,
        Cancelled,
    };

    struct Status
    {
        State    state                   = State::Idle;
        Error    error                   = Error::None;
        uint32_t files_total             = 0;
        uint32_t files_started           = 0;
        uint32_t files_succeeded         = 0;
        uint32_t files_failed            = 0;
        uint64_t bytes_total             = 0;
        uint64_t bytes_uploaded          = 0;
        bool     finished                = false;
        char     destination[kNameMaxLength] = {};
        char     current_file[kPathMaxLength] = {};
        char     message[kMessageMaxLength] = {};
    };

    struct PendingFile
    {
        char*        path = nullptr;
        uint64_t     size = 0;
        PendingFile* next = nullptr;
    };

    struct UrlParts
    {
        bool     https = false;
        char     host[kHostMaxLength] = {};
        char     path[kUrlMaxLength] = {};
        uint16_t port = 0;
    };

    using CompleteCallback = void (*)(const Status& status);

    CloudUpload();
    ~CloudUpload();

    CloudUpload(const CloudUpload&)            = delete;
    CloudUpload& operator=(const CloudUpload&) = delete;

    bool start(const cloud_item_t* destination, uint32_t timeout_ms = kDefaultTimeoutMs);
    bool uploadSingleFile(const cloud_item_t* destination, const char* path, uint32_t timeout_ms = kDefaultTimeoutMs);
    bool cancel();

    Status status() const;
    Error error() const;

    void setOnCompleteCallback(CompleteCallback callback);

    const char* stateName() const;
    const char* errorName() const;

private:
    enum class PendingUploadResult
    {
        Succeeded,
        Failed,
        Cancelled,
        FatalError,
    };

    static void taskEntry(void* argument);

    bool copyDestination(const cloud_item_t* destination, uint32_t timeout_ms);
    bool startTaskOnCurrentCore();
    void taskMain();
    bool uploadSingleFilePath(const char* path);
    PendingUploadResult uploadPendingFile(PendingFile& item, const UrlParts& url, uint64_t& committed_bytes, char* last_message, size_t last_message_size);
    void finish(State state, Error error, const char* message);
    void setStatusMessage(Error error, const char* message);

    mutable portMUX_TYPE m_lock            = portMUX_INITIALIZER_UNLOCKED;
    TaskHandle_t         m_task_handle     = nullptr;
    Status               m_status          = {};
    CompleteCallback     m_on_complete     = nullptr;
    volatile bool        m_cancel_requested = false;
    char                 m_destination_name[kNameMaxLength] = {};
    char                 m_destination_url[kUrlMaxLength] = {};
    char                 m_destination_password[kNetworkConfigPasswordMaxLength] = {};
    uint32_t             m_timeout_ms      = kDefaultTimeoutMs;
};
