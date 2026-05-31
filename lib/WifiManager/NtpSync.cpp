#include "NtpSync.h"

#include <Arduino.h>
#include <WiFi.h>
#include <string.h>
#include <time.h>

#include "ClockAgent.h"
#include "dbg_log.h"
#include "esp_sntp.h"

namespace
{

constexpr const char* TAG             = "NtpSync";
constexpr uint32_t    kPollIntervalMs  = 100;
constexpr uint32_t    kDestroyWaitMs   = 2000;
constexpr uint32_t    kTaskStackBytes  = 4096;

const char* status_name(NtpSync::Status status)
{
    switch (status)
    {
    case NtpSync::Status::Idle:
        return "Idle";
    case NtpSync::Status::Busy:
        return "Busy";
    case NtpSync::Status::Done:
        return "Done";
    case NtpSync::Status::Error:
        return "Error";
    case NtpSync::Status::Cancelled:
        return "Cancelled";
    default:
        return "Unknown";
    }
}

const char* error_name(NtpSync::Error error)
{
    switch (error)
    {
    case NtpSync::Error::None:
        return "None";
    case NtpSync::Error::AlreadyBusy:
        return "AlreadyBusy";
    case NtpSync::Error::InvalidArgument:
        return "InvalidArgument";
    case NtpSync::Error::WifiNotConnected:
        return "WifiNotConnected";
    case NtpSync::Error::TaskCreateFailed:
        return "TaskCreateFailed";
    case NtpSync::Error::Cancelled:
        return "Cancelled";
    case NtpSync::Error::Timeout:
        return "Timeout";
    case NtpSync::Error::RtcReadFailed:
        return "RtcReadFailed";
    case NtpSync::Error::NtpReadFailed:
        return "NtpReadFailed";
    case NtpSync::Error::RtcWriteFailed:
        return "RtcWriteFailed";
    case NtpSync::Error::NoNtpResult:
        return "NoNtpResult";
    default:
        return "Unknown";
    }
}

bool is_leap_year(int32_t year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int64_t days_from_civil(int32_t year, int32_t month, int32_t day)
{
    year -= month <= 2;
    const int32_t  era = (year >= 0 ? year : year - 399) / 400;
    const uint32_t yoe = static_cast<uint32_t>(year - era * 400);
    const uint32_t mp  = static_cast<uint32_t>(month + (month > 2 ? -3 : 9));
    const uint32_t doy = (153 * mp + 2) / 5 + static_cast<uint32_t>(day) - 1;
    const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

bool valid_date(const m5::rtc_date_t& date)
{
    if (date.year < 2020 || date.year > 2099 || date.month < 1 || date.month > 12)
    {
        return false;
    }

    static constexpr int8_t kMonthDays[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };

    int8_t max_day = kMonthDays[date.month - 1];
    if (date.month == 2 && is_leap_year(date.year))
    {
        ++max_day;
    }

    return date.date >= 1 && date.date <= max_day;
}

bool valid_time(const m5::rtc_time_t& time)
{
    return time.hours >= 0 && time.hours <= 23 && time.minutes >= 0 && time.minutes <= 59 && time.seconds >= 0 && time.seconds <= 59;
}

bool valid_datetime(const m5::rtc_datetime_t& datetime)
{
    return valid_date(datetime.date) && valid_time(datetime.time);
}

int64_t datetime_to_epoch_seconds(const m5::rtc_datetime_t& datetime)
{
    const int64_t days = days_from_civil(datetime.date.year, datetime.date.month, datetime.date.date);
    return days * 86400LL + static_cast<int64_t>(datetime.time.hours) * 3600LL + static_cast<int64_t>(datetime.time.minutes) * 60LL + datetime.time.seconds;
}

int64_t datetime_delta_seconds(const m5::rtc_datetime_t& lhs, const m5::rtc_datetime_t& rhs)
{
    int64_t delta = datetime_to_epoch_seconds(lhs) - datetime_to_epoch_seconds(rhs);
    return delta < 0 ? -delta : delta;
}

m5::rtc_datetime_t tm_to_datetime(const tm& value)
{
    m5::rtc_datetime_t datetime = {};
    datetime.date.year          = static_cast<int16_t>(value.tm_year + 1900);
    datetime.date.month         = static_cast<int8_t>(value.tm_mon + 1);
    datetime.date.date          = static_cast<int8_t>(value.tm_mday);
    datetime.date.weekDay       = static_cast<int8_t>(value.tm_wday);
    datetime.time.hours         = static_cast<int8_t>(value.tm_hour);
    datetime.time.minutes       = static_cast<int8_t>(value.tm_min);
    datetime.time.seconds       = static_cast<int8_t>(value.tm_sec);
    return datetime;
}

bool copy_text(char* dst, size_t dst_size, const char* src)
{
    if (!dst || dst_size == 0)
    {
        return false;
    }

    const char* value  = src ? src : "";
    const size_t chars = strlen(value);
    if (chars >= dst_size)
    {
        return false;
    }

    memcpy(dst, value, chars + 1);
    return true;
}

bool write_rtc_datetime(const m5::rtc_datetime_t& datetime)
{
    if (!valid_datetime(datetime))
    {
        return false;
    }

    Clock.setDateTime(datetime);

    m5::rtc_datetime_t verified = {};
    return M5.Rtc.getDateTime(&verified) && valid_datetime(verified) && datetime_delta_seconds(datetime, verified) <= 2;
}

} // namespace

NtpSync::NtpSync()
{
    m_result.status = Status::Idle;
    m_result.error  = Error::None;
    copy_text(m_timezone, sizeof(m_timezone), "UTC0");
}

NtpSync::~NtpSync()
{
    cancel();

    const uint32_t started_ms = millis();
    while (millis() - started_ms < kDestroyWaitMs)
    {
        portENTER_CRITICAL(&m_lock);
        const TaskHandle_t task_handle = m_task_handle;
        portEXIT_CRITICAL(&m_lock);

        if (!task_handle || task_handle == xTaskGetCurrentTaskHandle())
        {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

bool NtpSync::start(const WifiManager& wifi_manager, uint32_t timeout_ms, bool write_rtc_after_sync)
{
    portENTER_CRITICAL(&m_lock);
    const bool busy = m_status == Status::Busy;
    portEXIT_CRITICAL(&m_lock);

    if (busy)
    {
        portENTER_CRITICAL(&m_lock);
        m_error        = Error::AlreadyBusy;
        m_result.error = Error::AlreadyBusy;
        portEXIT_CRITICAL(&m_lock);
        return false;
    }

    if (!copyConfig(wifi_manager, timeout_ms, write_rtc_after_sync))
    {
        portENTER_CRITICAL(&m_lock);
        m_status        = Status::Error;
        m_error         = Error::InvalidArgument;
        m_result.status = Status::Error;
        m_result.error  = Error::InvalidArgument;
        portEXIT_CRITICAL(&m_lock);
        return false;
    }

    portENTER_CRITICAL(&m_lock);
    m_cancel_requested = false;
    m_status           = Status::Busy;
    m_error            = Error::None;
    m_result           = {};
    m_result.status    = Status::Busy;
    m_result.error     = Error::None;
    portEXIT_CRITICAL(&m_lock);

    if (!startTaskOnCurrentCore())
    {
        finish(Status::Error, Error::TaskCreateFailed);
        return false;
    }

    return true;
}

bool NtpSync::cancel()
{
    portENTER_CRITICAL(&m_lock);
    const bool busy = m_status == Status::Busy && m_task_handle != nullptr;
    if (busy)
    {
        m_cancel_requested = true;
    }
    portEXIT_CRITICAL(&m_lock);
    return busy;
}

bool NtpSync::writeRtc()
{
    Result current = {};

    portENTER_CRITICAL(&m_lock);
    if (m_status == Status::Busy)
    {
        m_error        = Error::AlreadyBusy;
        m_result.error = Error::AlreadyBusy;
        portEXIT_CRITICAL(&m_lock);
        return false;
    }

    if (!m_result.ntp_time_valid)
    {
        m_status        = Status::Error;
        m_error         = Error::NoNtpResult;
        m_result.status = Status::Error;
        m_result.error  = Error::NoNtpResult;
        portEXIT_CRITICAL(&m_lock);
        return false;
    }

    m_result.rtc_write_requested = true;
    current                      = m_result;
    portEXIT_CRITICAL(&m_lock);

    const bool ok = write_rtc_datetime(current.ntp_time);

    portENTER_CRITICAL(&m_lock);
    m_result.rtc_write_succeeded = ok;
    if (ok)
    {
        m_result.rtc_time           = current.ntp_time;
        m_result.rtc_time_valid     = true;
        m_result.rtc_offset_seconds = 0;
    }
    else
    {
        m_status        = Status::Error;
        m_error         = Error::RtcWriteFailed;
        m_result.status = Status::Error;
        m_result.error  = Error::RtcWriteFailed;
    }
    portEXIT_CRITICAL(&m_lock);

    return ok;
}

NtpSync::Status NtpSync::status() const
{
    portENTER_CRITICAL(&m_lock);
    const Status value = m_status;
    portEXIT_CRITICAL(&m_lock);
    return value;
}

NtpSync::Error NtpSync::error() const
{
    portENTER_CRITICAL(&m_lock);
    const Error value = m_error;
    portEXIT_CRITICAL(&m_lock);
    return value;
}

NtpSync::Result NtpSync::result() const
{
    portENTER_CRITICAL(&m_lock);
    const Result value = m_result;
    portEXIT_CRITICAL(&m_lock);
    return value;
}

bool NtpSync::rtcOffsetSeconds(int64_t* offset_seconds) const
{
    if (!offset_seconds)
    {
        return false;
    }

    portENTER_CRITICAL(&m_lock);
    const bool ok = m_result.rtc_time_valid && m_result.ntp_time_valid;
    if (ok)
    {
        *offset_seconds = m_result.rtc_offset_seconds;
    }
    portEXIT_CRITICAL(&m_lock);
    return ok;
}

int64_t NtpSync::rtcOffsetSeconds() const
{
    int64_t offset_seconds = 0;
    rtcOffsetSeconds(&offset_seconds);
    return offset_seconds;
}

void NtpSync::setOnCompleteCallback(CompleteCallback callback)
{
    portENTER_CRITICAL(&m_lock);
    m_on_complete = callback;
    portEXIT_CRITICAL(&m_lock);
}

const char* NtpSync::statusName() const
{
    return status_name(status());
}

const char* NtpSync::errorName() const
{
    return error_name(error());
}

void NtpSync::taskEntry(void* argument)
{
    NtpSync* sync = static_cast<NtpSync*>(argument);
    if (sync)
    {
        sync->taskMain();
    }
    vTaskDelete(nullptr);
}

bool NtpSync::copyConfig(const WifiManager& wifi_manager, uint32_t timeout_ms, bool write_rtc_after_sync)
{
    if (!copy_text(m_timezone, sizeof(m_timezone), wifi_manager.timezone()))
    {
        return false;
    }

    bool has_server = false;
    for (size_t i = 0; i < kServerCount; ++i)
    {
        const char* server = wifi_manager.ntpServer(i);
        if (!copy_text(m_servers[i], sizeof(m_servers[i]), server))
        {
            return false;
        }

        has_server = has_server || (m_servers[i][0] != '\0');
    }

    if (!has_server)
    {
        return false;
    }

    m_timeout_ms           = timeout_ms == 0 ? kDefaultTimeoutMs : timeout_ms;
    m_write_rtc_after_sync = write_rtc_after_sync;
    return true;
}

bool NtpSync::startTaskOnCurrentCore()
{
    TaskHandle_t task_handle = nullptr;
    const BaseType_t created = xTaskCreatePinnedToCore(taskEntry,
                                                       "NtpSync",
                                                       kTaskStackBytes,
                                                       this,
                                                       1,
                                                       &task_handle,
                                                       xPortGetCoreID());

    if (created != pdPASS)
    {
        return false;
    }

    portENTER_CRITICAL(&m_lock);
    if (m_status == Status::Busy)
    {
        m_task_handle = task_handle;
    }
    portEXIT_CRITICAL(&m_lock);
    return true;
}

void NtpSync::taskMain()
{
    Result local_result       = {};
    local_result.status       = Status::Busy;
    local_result.error        = Error::None;
    local_result.rtc_write_requested = m_write_rtc_after_sync;

    if (WiFi.status() != WL_CONNECTED)
    {
        finish(Status::Error, Error::WifiNotConnected);
        return;
    }

    local_result.rtc_time_valid = M5.Rtc.getDateTime(&local_result.rtc_time) && valid_datetime(local_result.rtc_time);
    if (!local_result.rtc_time_valid)
    {
        portENTER_CRITICAL(&m_lock);
        m_result = local_result;
        portEXIT_CRITICAL(&m_lock);
        finish(Status::Error, Error::RtcReadFailed);
        return;
    }

    esp_sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
    configTzTime(m_timezone,
                 m_servers[0][0] ? m_servers[0] : nullptr,
                 m_servers[1][0] ? m_servers[1] : nullptr,
                 m_servers[2][0] ? m_servers[2] : nullptr);

    const uint32_t started_ms = millis();
    while (millis() - started_ms < m_timeout_ms)
    {
        portENTER_CRITICAL(&m_lock);
        const bool cancel_requested = m_cancel_requested;
        portEXIT_CRITICAL(&m_lock);

        if (cancel_requested)
        {
            portENTER_CRITICAL(&m_lock);
            m_result = local_result;
            portEXIT_CRITICAL(&m_lock);
            finish(Status::Cancelled, Error::Cancelled);
            return;
        }

        const sntp_sync_status_t sync_status = esp_sntp_get_sync_status();
        if (sync_status == SNTP_SYNC_STATUS_COMPLETED)
        {
            time_t now      = time(nullptr);
            tm     local_tm = {};
            if (now > 0 && localtime_r(&now, &local_tm))
            {
                local_result.ntp_time = tm_to_datetime(local_tm);
                if (valid_datetime(local_result.ntp_time))
                {
                    local_result.ntp_time_valid     = true;
                    local_result.rtc_offset_seconds = datetime_to_epoch_seconds(local_result.ntp_time) - datetime_to_epoch_seconds(local_result.rtc_time);
                    break;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(kPollIntervalMs));
    }

    if (!local_result.ntp_time_valid)
    {
        portENTER_CRITICAL(&m_lock);
        m_result = local_result;
        portEXIT_CRITICAL(&m_lock);
        finish(Status::Error, Error::Timeout);
        return;
    }

    if (m_write_rtc_after_sync)
    {
        local_result.rtc_write_succeeded = write_rtc_datetime(local_result.ntp_time);
        if (!local_result.rtc_write_succeeded)
        {
            portENTER_CRITICAL(&m_lock);
            m_result = local_result;
            portEXIT_CRITICAL(&m_lock);
            finish(Status::Error, Error::RtcWriteFailed);
            return;
        }
    }

    portENTER_CRITICAL(&m_lock);
    m_result = local_result;
    portEXIT_CRITICAL(&m_lock);

    DBG_LOGI(TAG, "NTP sync done, RTC offset=%lld seconds", static_cast<long long>(local_result.rtc_offset_seconds));
    finish(Status::Done, Error::None);
}

void NtpSync::finish(Status status, Error error)
{
    CompleteCallback callback = nullptr;
    Result           callback_result = {};

    portENTER_CRITICAL(&m_lock);
    m_status        = status;
    m_error         = error;
    m_task_handle   = nullptr;
    m_result.status = status;
    m_result.error  = error;
    callback        = m_on_complete;
    callback_result = m_result;
    portEXIT_CRITICAL(&m_lock);

    if (callback)
    {
        callback(callback_result);
    }
}
