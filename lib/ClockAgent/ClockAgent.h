#pragma once

#include <M5Unified.h>
#include <stdint.h>
#include <time.h>

class ClockAgent
{
public:
    bool begin();
    bool syncFromRtc();

    bool getDateTime(m5::rtc_date_t* date, m5::rtc_time_t* time);
    bool getDateTime(m5::rtc_datetime_t* datetime);
    bool getDate(m5::rtc_date_t* date);
    bool getTime(m5::rtc_time_t* time);

    m5::rtc_datetime_t getDateTime();
    m5::rtc_date_t     getDate();
    m5::rtc_time_t     getTime();

    void setDateTime(const tm* datetime);
    void setDateTime(const m5::rtc_date_t* date, const m5::rtc_time_t* time);
    void setDateTime(const m5::rtc_datetime_t* datetime);
    void setDateTime(const m5::rtc_datetime_t& datetime);
    void setDate(const m5::rtc_date_t* date);
    void setDate(const m5::rtc_date_t& date);
    void setTime(const m5::rtc_time_t* time);
    void setTime(const m5::rtc_time_t& time);

    bool isSynced() const;

private:
    bool               ensureSynced();
    m5::rtc_datetime_t currentDateTime() const;

    int64_t  baseEpochSeconds_ = 0;
    uint32_t baseMillis_       = 0;
    bool     synced_           = false;
};

extern ClockAgent Clock;
