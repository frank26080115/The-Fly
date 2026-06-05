#include "ClockAgent.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "esp_random.h"
#include "thefly_version.h"
#include "utilfuncs.h"

namespace
{

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static bool               valid_date(const m5::rtc_date_t& date);
static bool               valid_time(const m5::rtc_time_t& time);
static int64_t            datetime_to_epoch_seconds(const m5::rtc_datetime_t& datetime);
static m5::rtc_datetime_t epoch_seconds_to_datetime(int64_t epoch_seconds);
#ifdef TEST_MOCK_SCRAMBLE_TIME
static int64_t            mock_scrambled_epoch_seconds(int64_t epoch_seconds);
#endif
static m5::rtc_datetime_t tm_to_datetime(const tm& value);
static bool               parse_compiler_time(const char* text, m5::rtc_datetime_t& datetime);
static m5::rtc_datetime_t
merge_datetime(const m5::rtc_datetime_t& base, const m5::rtc_date_t* date, const m5::rtc_time_t* time);
static bool   datetime_to_local_epoch_seconds(const m5::rtc_datetime_t& datetime, time_t& epoch_seconds);
static bool   apply_system_time(time_t epoch_seconds);
static int8_t month_from_name(const char* month);

#ifdef TEST_MOCK_SCRAMBLE_TIME
static bool g_mock_time_scrambled_this_boot = false;
#endif

} // namespace

ClockAgent Clock;

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

bool ClockAgent::begin()
{
    return syncFromRtc();
}

bool ClockAgent::syncFromRtc()
{
    m5::rtc_datetime_t utc_datetime;
    if (!M5.Rtc.getDateTime(&utc_datetime) || !valid_date(utc_datetime.date) || !valid_time(utc_datetime.time))
    {
        synced_ = false;
        return false;
    }

#ifdef TEST_MOCK_SCRAMBLE_TIME
    baseEpochSeconds_ = mock_scrambled_epoch_seconds(datetime_to_epoch_seconds(utc_datetime));
#else
    baseEpochSeconds_ = datetime_to_epoch_seconds(utc_datetime);
#endif
    baseMillis_       = millis();
    synced_           = true;
    apply_system_time(static_cast<time_t>(baseEpochSeconds_));
    return true;
}

bool ClockAgent::syncToCompileTime()
{
    m5::rtc_datetime_t compile_datetime = {};
    if (!parse_compiler_time(compiler_time_str, compile_datetime))
    {
        return false;
    }

    time_t compile_epoch_seconds = 0;
    if (!datetime_to_local_epoch_seconds(compile_datetime, compile_epoch_seconds))
    {
        return false;
    }

    m5::rtc_datetime_t rtc_datetime = {};
    const bool         rtc_valid =
        M5.Rtc.getDateTime(&rtc_datetime) && valid_date(rtc_datetime.date) && valid_time(rtc_datetime.time);
    if (!rtc_valid || static_cast<int64_t>(compile_epoch_seconds) > datetime_to_epoch_seconds(rtc_datetime))
    {
        return setUnixTime(compile_epoch_seconds);
    }

    baseEpochSeconds_ = datetime_to_epoch_seconds(rtc_datetime);
    baseMillis_       = millis();
    synced_           = true;
    apply_system_time(static_cast<time_t>(baseEpochSeconds_));
    return true;
}

bool ClockAgent::ensureSynced()
{
    return synced_ || syncFromRtc();
}

bool ClockAgent::getDateTime(m5::rtc_date_t* date, m5::rtc_time_t* time)
{
    if (!ensureSynced())
    {
        return false;
    }

    const m5::rtc_datetime_t datetime = currentDateTime();
    if (!valid_date(datetime.date) || !valid_time(datetime.time))
    {
        return false;
    }
    if (date)
    {
        *date = datetime.date;
    }
    if (time)
    {
        *time = datetime.time;
    }
    return true;
}

bool ClockAgent::getDateTime(m5::rtc_datetime_t* datetime)
{
    return datetime ? getDateTime(&datetime->date, &datetime->time) : false;
}

bool ClockAgent::getDate(m5::rtc_date_t* date)
{
    return getDateTime(date, nullptr);
}

bool ClockAgent::getTime(m5::rtc_time_t* time)
{
    return getDateTime(nullptr, time);
}

m5::rtc_datetime_t ClockAgent::getDateTime()
{
    m5::rtc_datetime_t datetime = {};
    getDateTime(&datetime);
    return datetime;
}

m5::rtc_date_t ClockAgent::getDate()
{
    m5::rtc_date_t date = {};
    getDate(&date);
    return date;
}

m5::rtc_time_t ClockAgent::getTime()
{
    m5::rtc_time_t time = {};
    getTime(&time);
    return time;
}

bool ClockAgent::getUnixTime(time_t* epoch_seconds)
{
    if (!epoch_seconds || !ensureSynced())
    {
        return false;
    }

    const int64_t current = currentEpochSeconds();
    *epoch_seconds        = static_cast<time_t>(current);
    return static_cast<int64_t>(*epoch_seconds) == current;
}

time_t ClockAgent::getUnixTime()
{
    time_t epoch_seconds = 0;
    getUnixTime(&epoch_seconds);
    return epoch_seconds;
}

void ClockAgent::setDateTime(const tm* datetime)
{
    if (!datetime)
    {
        return;
    }

    const m5::rtc_datetime_t converted = tm_to_datetime(*datetime);
    setDateTime(converted);
}

void ClockAgent::setDateTime(const m5::rtc_date_t* date, const m5::rtc_time_t* time)
{
    if (!date && !time)
    {
        return;
    }

    m5::rtc_datetime_t base = {};
    if (!getDateTime(&base))
    {
        return;
    }
    const m5::rtc_datetime_t merged = merge_datetime(base, date, time);
    setDateTime(merged);
}

void ClockAgent::setDateTime(const m5::rtc_datetime_t* datetime)
{
    if (datetime)
    {
        setDateTime(*datetime);
    }
}

void ClockAgent::setDateTime(const m5::rtc_datetime_t& datetime)
{
    time_t epoch_seconds = 0;
    if (!datetime_to_local_epoch_seconds(datetime, epoch_seconds))
    {
        synced_ = false;
        return;
    }

    setUnixTime(epoch_seconds);
}

void ClockAgent::setDate(const m5::rtc_date_t* date)
{
    setDateTime(date, nullptr);
}

void ClockAgent::setDate(const m5::rtc_date_t& date)
{
    setDate(&date);
}

void ClockAgent::setTime(const m5::rtc_time_t* time)
{
    setDateTime(nullptr, time);
}

void ClockAgent::setTime(const m5::rtc_time_t& time)
{
    setTime(&time);
}

bool ClockAgent::setUnixTime(time_t epoch_seconds)
{
    const m5::rtc_datetime_t utc_datetime = epoch_seconds_to_datetime(static_cast<int64_t>(epoch_seconds));
    if (!valid_date(utc_datetime.date) || !valid_time(utc_datetime.time))
    {
        return false;
    }

    M5.Rtc.setDateTime(utc_datetime);
    baseEpochSeconds_ = static_cast<int64_t>(epoch_seconds);
    baseMillis_       = millis();
    synced_           = true;
    apply_system_time(epoch_seconds);
    return synced_;
}

bool ClockAgent::isSynced() const
{
    return synced_;
}

bool ClockAgent::ensureSystemTimeForTls()
{
    if (!ensureSynced() && !syncToCompileTime())
    {
        return false;
    }

    return apply_system_time(static_cast<time_t>(currentEpochSeconds()));
}

int64_t ClockAgent::currentEpochSeconds() const
{
    const uint32_t elapsed_ms = millis() - baseMillis_;
    return baseEpochSeconds_ + elapsed_ms / 1000U;
}

m5::rtc_datetime_t ClockAgent::currentDateTime() const
{
    const int64_t epoch_seconds = currentEpochSeconds();
    const time_t  as_time_t     = static_cast<time_t>(epoch_seconds);
    tm            local_tm      = {};
    if (static_cast<int64_t>(as_time_t) == epoch_seconds && localtime_r(&as_time_t, &local_tm))
    {
        return tm_to_datetime(local_tm);
    }

    return epoch_seconds_to_datetime(epoch_seconds);
}

namespace
{

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

bool valid_date(const m5::rtc_date_t& date)
{
    if (date.year < 1900 || date.year > 2099 || date.month < 1 || date.month > 12)
    {
        return false;
    }

    static constexpr int8_t kMonthDays[] = {
        31,
        28,
        31,
        30,
        31,
        30,
        31,
        31,
        30,
        31,
        30,
        31,
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
    return time.hours >= 0 && time.hours <= 23 && time.minutes >= 0 && time.minutes <= 59 && time.seconds >= 0 &&
           time.seconds <= 59;
}

int64_t datetime_to_epoch_seconds(const m5::rtc_datetime_t& datetime)
{
    const int64_t days = days_from_civil(datetime.date.year, datetime.date.month, datetime.date.date);
    return days * 86400LL + static_cast<int64_t>(datetime.time.hours) * 3600LL +
           static_cast<int64_t>(datetime.time.minutes) * 60LL + datetime.time.seconds;
}

m5::rtc_datetime_t epoch_seconds_to_datetime(int64_t epoch_seconds)
{
    int64_t days           = epoch_seconds / 86400LL;
    int64_t seconds_of_day = epoch_seconds % 86400LL;
    if (seconds_of_day < 0)
    {
        seconds_of_day += 86400LL;
        --days;
    }

    int32_t year;
    int32_t month;
    int32_t day;
    civil_from_days(days, year, month, day);

    m5::rtc_datetime_t datetime;
    datetime.date.year    = static_cast<int16_t>(year);
    datetime.date.month   = static_cast<int8_t>(month);
    datetime.date.date    = static_cast<int8_t>(day);
    datetime.date.weekDay = weekday_from_days(days);
    datetime.time.hours   = static_cast<int8_t>(seconds_of_day / 3600LL);
    datetime.time.minutes = static_cast<int8_t>((seconds_of_day / 60LL) % 60LL);
    datetime.time.seconds = static_cast<int8_t>(seconds_of_day % 60LL);
    return datetime;
}

#ifdef TEST_MOCK_SCRAMBLE_TIME
int64_t mock_scrambled_epoch_seconds(int64_t epoch_seconds)
{
    if (g_mock_time_scrambled_this_boot)
    {
        return epoch_seconds;
    }
    g_mock_time_scrambled_this_boot = true;

    static constexpr int64_t kMinValidEpochSeconds = -2208988800LL; // 1900-01-01
    static constexpr int64_t kMaxValidEpochSeconds = 4102444799LL; // 2099-12-31 23:59:59
    static constexpr int64_t kMinOffsetSeconds     = 6LL * 60LL * 60LL;
    static constexpr int64_t kOffsetRangeSeconds   = 14LL * 24LL * 60LL * 60LL;

    const uint32_t random_value = esp_random();
    const int64_t  magnitude    = kMinOffsetSeconds + static_cast<int64_t>(random_value % kOffsetRangeSeconds);
    int64_t        offset       = (random_value & 0x80000000U) ? magnitude : -magnitude;

    if (epoch_seconds + offset < kMinValidEpochSeconds || epoch_seconds + offset > kMaxValidEpochSeconds)
    {
        offset = -offset;
    }
    if (epoch_seconds + offset < kMinValidEpochSeconds || epoch_seconds + offset > kMaxValidEpochSeconds)
    {
        offset = kMinOffsetSeconds;
    }

    const int64_t scrambled = epoch_seconds + offset;
    DBG_LOGW("ClockAgent",
             "TEST_MOCK_SCRAMBLE_TIME: RTC epoch=%lld scrambled=%lld offset=%lld seconds",
             static_cast<long long>(epoch_seconds),
             static_cast<long long>(scrambled),
             static_cast<long long>(offset));
    return scrambled;
}
#endif

m5::rtc_datetime_t tm_to_datetime(const tm& value)
{
    m5::rtc_datetime_t datetime;
    datetime.date.year    = static_cast<int16_t>(value.tm_year + 1900);
    datetime.date.month   = static_cast<int8_t>(value.tm_mon + 1);
    datetime.date.date    = static_cast<int8_t>(value.tm_mday);
    datetime.date.weekDay = static_cast<int8_t>(value.tm_wday);
    datetime.time.hours   = static_cast<int8_t>(value.tm_hour);
    datetime.time.minutes = static_cast<int8_t>(value.tm_min);
    datetime.time.seconds = static_cast<int8_t>(value.tm_sec);
    return datetime;
}

bool parse_compiler_time(const char* text, m5::rtc_datetime_t& datetime)
{
    char month_name[4] = {};
    int  year          = 0;
    int  month_day     = 0;
    int  hours         = 0;
    int  minutes       = 0;
    int  seconds       = 0;

    if (!text || sscanf(text, "%3s %d %d %d:%d:%d", month_name, &month_day, &year, &hours, &minutes, &seconds) != 6)
    {
        return false;
    }

    const int8_t month = month_from_name(month_name);
    if (month == 0)
    {
        return false;
    }

    datetime.date.year    = static_cast<int16_t>(year);
    datetime.date.month   = month;
    datetime.date.date    = static_cast<int8_t>(month_day);
    datetime.time.hours   = static_cast<int8_t>(hours);
    datetime.time.minutes = static_cast<int8_t>(minutes);
    datetime.time.seconds = static_cast<int8_t>(seconds);

    if (!valid_date(datetime.date) || !valid_time(datetime.time))
    {
        return false;
    }

    datetime.date.weekDay = weekday_from_days(days_from_civil(year, month, month_day));
    return true;
}

m5::rtc_datetime_t
merge_datetime(const m5::rtc_datetime_t& base, const m5::rtc_date_t* date, const m5::rtc_time_t* time)
{
    m5::rtc_datetime_t merged = base;
    if (date)
    {
        merged.date = *date;
    }
    if (time)
    {
        merged.time = *time;
    }

    if (valid_date(merged.date))
    {
        const int64_t days  = days_from_civil(merged.date.year, merged.date.month, merged.date.date);
        merged.date.weekDay = weekday_from_days(days);
    }

    return merged;
}

bool datetime_to_local_epoch_seconds(const m5::rtc_datetime_t& datetime, time_t& epoch_seconds)
{
    if (!valid_date(datetime.date) || !valid_time(datetime.time))
    {
        return false;
    }

    tm value       = {};
    value.tm_year  = datetime.date.year - 1900;
    value.tm_mon   = datetime.date.month - 1;
    value.tm_mday  = datetime.date.date;
    value.tm_hour  = datetime.time.hours;
    value.tm_min   = datetime.time.minutes;
    value.tm_sec   = datetime.time.seconds;
    value.tm_isdst = -1;

    const time_t converted = mktime(&value);
    if (converted == static_cast<time_t>(-1))
    {
        return false;
    }

    epoch_seconds = converted;
    return true;
}

bool apply_system_time(time_t epoch_seconds)
{
    struct timeval value = {};
    value.tv_sec         = epoch_seconds;
    return settimeofday(&value, nullptr) == 0;
}

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

int8_t month_from_name(const char* month)
{
    static constexpr const char* kMonths[] = {
        "Jan",
        "Feb",
        "Mar",
        "Apr",
        "May",
        "Jun",
        "Jul",
        "Aug",
        "Sep",
        "Oct",
        "Nov",
        "Dec",
    };

    if (!month)
    {
        return 0;
    }

    for (size_t i = 0; i < sizeof(kMonths) / sizeof(kMonths[0]); ++i)
    {
        if (strncmp(month, kMonths[i], 3) == 0)
        {
            return static_cast<int8_t>(i + 1);
        }
    }

    return 0;
}

} // namespace
