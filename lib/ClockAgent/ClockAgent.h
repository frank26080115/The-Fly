#pragma once

#include "thefly_common.h"

#include <M5Unified.h>
#include <stdint.h>
#include <time.h>

class ClockAgent
{
public:
    bool begin();
    bool syncFromRtc();
    bool syncToCompileTime();

    bool getDateTime(m5::rtc_date_t* date, m5::rtc_time_t* time);
    bool getDateTime(m5::rtc_datetime_t* datetime);
    bool getDate(m5::rtc_date_t* date);
    bool getTime(m5::rtc_time_t* time);

    m5::rtc_datetime_t getDateTime();
    m5::rtc_date_t     getDate();
    m5::rtc_time_t     getTime();
    bool               getUnixTime(time_t* epoch_seconds);
    time_t             getUnixTime();

    void setDateTime(const tm* datetime);
    void setDateTime(const m5::rtc_date_t* date, const m5::rtc_time_t* time);
    void setDateTime(const m5::rtc_datetime_t* datetime);
    void setDateTime(const m5::rtc_datetime_t& datetime);
    void setDate(const m5::rtc_date_t* date);
    void setDate(const m5::rtc_date_t& date);
    void setTime(const m5::rtc_time_t* time);
    void setTime(const m5::rtc_time_t& time);
    bool setUnixTime(time_t epoch_seconds);

    bool isSynced() const;
    bool ensureSystemTimeForTls();

private:
    bool               ensureSynced();
    m5::rtc_datetime_t currentDateTime() const;

    int64_t  baseEpochSeconds_ = 0;
    uint32_t baseMillis_       = 0;
    bool     synced_           = false;
};

extern ClockAgent Clock;
