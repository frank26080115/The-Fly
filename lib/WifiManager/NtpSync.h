#pragma once

#include <M5Unified.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "WifiManager.h"

class NtpSync
{
public:
    static constexpr size_t kServerCount       = WifiManager::kNtpServerCount;
    static constexpr size_t kTimezoneMaxLength = 64;
    static constexpr size_t kServerMaxLength   = 128;
    static constexpr uint32_t kDefaultTimeoutMs = 15000;

    enum class Status
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
        WifiNotConnected,
        TaskCreateFailed,
        Cancelled,
        Timeout,
        RtcReadFailed,
        NtpReadFailed,
        RtcWriteFailed,
        NoNtpResult,
    };

    struct Result
    {
        Status             status               = Status::Idle;
        Error              error                = Error::None;
        m5::rtc_datetime_t rtc_time             = {};
        m5::rtc_datetime_t ntp_time             = {};
        int64_t            rtc_offset_seconds   = 0;
        bool               rtc_time_valid       = false;
        bool               ntp_time_valid       = false;
        bool               rtc_write_requested  = false;
        bool               rtc_write_succeeded  = false;
    };

    using CompleteCallback = void (*)(const Result& result);

    NtpSync();
    ~NtpSync();

    NtpSync(const NtpSync&)            = delete;
    NtpSync& operator=(const NtpSync&) = delete;

    bool start(const WifiManager& wifi_manager,
               uint32_t timeout_ms = kDefaultTimeoutMs,
               bool write_rtc_after_sync = false);

    bool cancel();
    bool writeRtc();

    Status status() const;
    Error error() const;
    Result result() const;

    bool rtcOffsetSeconds(int64_t* offset_seconds) const;
    int64_t rtcOffsetSeconds() const;

    void setOnCompleteCallback(CompleteCallback callback);

    const char* statusName() const;
    const char* errorName() const;

private:
    static void taskEntry(void* argument);

    bool copyConfig(const WifiManager& wifi_manager, uint32_t timeout_ms, bool write_rtc_after_sync);
    bool startTaskOnCurrentCore();
    void taskMain();
    void finish(Status status, Error error);

    mutable portMUX_TYPE m_lock                 = portMUX_INITIALIZER_UNLOCKED;
    TaskHandle_t         m_task_handle          = nullptr;
    Status               m_status               = Status::Idle;
    Error                m_error                = Error::None;
    Result               m_result               = {};
    CompleteCallback     m_on_complete          = nullptr;
    volatile bool        m_cancel_requested     = false;
    char                 m_timezone[kTimezoneMaxLength] = {};
    char                 m_servers[kServerCount][kServerMaxLength] = {};
    uint32_t             m_timeout_ms           = kDefaultTimeoutMs;
    bool                 m_write_rtc_after_sync = false;
};
