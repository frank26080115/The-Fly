#include "ClockAgent.h"

namespace
{

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

void civil_from_days(int64_t days, int32_t& year, int32_t& month, int32_t& day)
{
    days += 719468;
    const int64_t  era = (days >= 0 ? days : days - 146096) / 146097;
    const uint32_t doe = static_cast<uint32_t>(days - era * 146097);
    const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    year               = static_cast<int32_t>(yoe) + static_cast<int32_t>(era) * 400;
    const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const uint32_t mp  = (5 * doy + 2) / 153;
    day                = static_cast<int32_t>(doy - (153 * mp + 2) / 5 + 1);
    month              = static_cast<int32_t>(mp + (mp < 10 ? 3 : -9));
    year += month <= 2;
}

int8_t weekday_from_days(int64_t days)
{
    int64_t weekday = (days + 4) % 7;
    if (weekday < 0)
    {
        weekday += 7;
    }
    return static_cast<int8_t>(weekday);
}

bool valid_date(const m5::rtc_date_t& date)
{
    if (date.year < 1900 || date.year > 2099 || date.month < 1 || date.month > 12)
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

int64_t datetime_to_epoch_seconds(const m5::rtc_datetime_t& datetime)
{
    const int64_t days = days_from_civil(datetime.date.year, datetime.date.month, datetime.date.date);
    return days * 86400LL + static_cast<int64_t>(datetime.time.hours) * 3600LL + static_cast<int64_t>(datetime.time.minutes) * 60LL + datetime.time.seconds;
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

m5::rtc_datetime_t merge_datetime(const m5::rtc_datetime_t& base, const m5::rtc_date_t* date, const m5::rtc_time_t* time)
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

} // namespace

ClockAgent Clock;

bool ClockAgent::begin()
{
    return syncFromRtc();
}

bool ClockAgent::syncFromRtc()
{
    m5::rtc_datetime_t datetime;
    if (!M5.Rtc.getDateTime(&datetime) || !valid_date(datetime.date) || !valid_time(datetime.time))
    {
        synced_ = false;
        return false;
    }

    baseEpochSeconds_ = datetime_to_epoch_seconds(datetime);
    baseMillis_       = millis();
    synced_           = true;
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
    m5::rtc_datetime_t datetime;
    getDateTime(&datetime);
    return datetime;
}

m5::rtc_date_t ClockAgent::getDate()
{
    m5::rtc_date_t date;
    getDate(&date);
    return date;
}

m5::rtc_time_t ClockAgent::getTime()
{
    m5::rtc_time_t time;
    getTime(&time);
    return time;
}

void ClockAgent::setDateTime(const tm* datetime)
{
    if (!datetime)
    {
        return;
    }

    const m5::rtc_datetime_t converted(*datetime);
    setDateTime(converted);
}

void ClockAgent::setDateTime(const m5::rtc_date_t* date, const m5::rtc_time_t* time)
{
    if (!date && !time)
    {
        return;
    }

    m5::rtc_datetime_t       base   = getDateTime();
    const m5::rtc_datetime_t merged = merge_datetime(base, date, time);
    M5.Rtc.setDateTime(date ? &merged.date : nullptr, time ? &merged.time : nullptr);
    baseEpochSeconds_ = datetime_to_epoch_seconds(merged);
    baseMillis_       = millis();
    synced_           = true;
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
    M5.Rtc.setDateTime(datetime);
    baseEpochSeconds_ = datetime_to_epoch_seconds(datetime);
    baseMillis_       = millis();
    synced_           = valid_date(datetime.date) && valid_time(datetime.time);
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

bool ClockAgent::isSynced() const
{
    return synced_;
}

m5::rtc_datetime_t ClockAgent::currentDateTime() const
{
    const uint32_t elapsed_ms = millis() - baseMillis_;
    return epoch_seconds_to_datetime(baseEpochSeconds_ + elapsed_ms / 1000U);
}
