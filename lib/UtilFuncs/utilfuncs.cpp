#include "utilfuncs.h"
#include <Arduino.h>
#include "thefly_common.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"

namespace
{
constexpr const char* TAG = "utilfuncs";

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

int8_t weekday_from_days(int64_t days)
{
    int64_t weekday = (days + 4) % 7;
    if (weekday < 0)
    {
        weekday += 7;
    }
    return static_cast<int8_t>(weekday);
}

bool valid_date(int32_t year, int32_t month, int32_t day)
{
    if (year < 1900 || year > 2099 || month < 1 || month > 12)
    {
        return false;
    }

    static constexpr int8_t kMonthDays[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };

    int8_t max_day = kMonthDays[month - 1];
    if (month == 2 && is_leap_year(year))
    {
        ++max_day;
    }

    return day >= 1 && day <= max_day;
}

bool valid_time(int32_t hours, int32_t minutes, int32_t seconds)
{
    return hours >= 0 && hours <= 23 && minutes >= 0 && minutes <= 59 && seconds >= 0 && seconds <= 59;
}

int32_t parse_digits(const char* digits, size_t offset, size_t width, int32_t fallback)
{
    int32_t value = 0;
    for (size_t i = 0; i < width; ++i)
    {
        const char ch = digits[offset + i];
        if (ch < '0' || ch > '9')
        {
            return fallback;
        }
        value = value * 10 + (ch - '0');
    }
    return value;
}
}

bool ok(esp_err_t err, const char* what)
{
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE)
    {
        return true;
    }
    ESP_LOGE(TAG, "%s failed: %s", what, esp_err_to_name(err));
    return false;
}

bool strict_ok(esp_err_t err, const char* what)
{
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "%s ok", what);
        return true;
    }

    ESP_LOGE(TAG, "%s failed: %s", what, esp_err_to_name(err));
    return false;
}

void idle_forever()
{
    while (true)
    {
        delay(1000);
    }
}

const char* trim_start(const char* text)
{
    while (text && *text && isspace(static_cast<unsigned char>(*text)))
    {
        ++text;
    }

    return text;
}

size_t trimmed_length(const char* text)
{
    if (!text)
    {
        return 0;
    }

    size_t len = strlen(text);
    while (len > 0 && isspace(static_cast<unsigned char>(text[len - 1])))
    {
        --len;
    }

    return len;
}

char* clone_trimmed_string(const char* text)
{
    text             = trim_start(text);
    const size_t len = trimmed_length(text);
    if (len == 0)
    {
        return nullptr;
    }

    char* clone = static_cast<char*>(malloc(len + 1));
    if (!clone)
    {
        return nullptr;
    }

    memcpy(clone, text, len);
    clone[len] = '\0';
    return clone;
}

void copy_bda(uint8_t dst[ESP_BD_ADDR_LEN], const uint8_t src[ESP_BD_ADDR_LEN])
{
    memcpy(dst, src, ESP_BD_ADDR_LEN);
}

void log_bda(const char* label, const uint8_t bda[ESP_BD_ADDR_LEN])
{
    ESP_LOGI(TAG, "%s %02x:%02x:%02x:%02x:%02x:%02x", label, bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}

void format_bdaddr(const uint8_t bdaddr[ESP_BD_ADDR_LEN], char* out, size_t out_size)
{
    if (!out || out_size == 0)
    {
        return;
    }

    if (!bdaddr)
    {
        out[0] = '\0';
        return;
    }

    snprintf(out,
             out_size,
             "%02X:%02X:%02X:%02X:%02X:%02X",
             bdaddr[0],
             bdaddr[1],
             bdaddr[2],
             bdaddr[3],
             bdaddr[4],
             bdaddr[5]);
}

bool bda_equal(const uint8_t a[ESP_BD_ADDR_LEN], const uint8_t b[ESP_BD_ADDR_LEN])
{
    return memcmp(a, b, ESP_BD_ADDR_LEN) == 0;
}

void format_bytes(uint64_t bytes, char* out, size_t out_size)
{
    if (!out || out_size == 0)
    {
        return;
    }

    constexpr uint64_t kKiB = 1024ULL;
    constexpr uint64_t kMiB = kKiB * 1024ULL;
    constexpr uint64_t kGiB = kMiB * 1024ULL;

    const char* unit  = "B";
    uint64_t    scale = 1;
    if (bytes >= kGiB)
    {
        unit  = "G";
        scale = kGiB;
    }
    else if (bytes >= kMiB)
    {
        unit  = "M";
        scale = kMiB;
    }
    else if (bytes >= kKiB)
    {
        unit  = "K";
        scale = kKiB;
    }

    const uint64_t value = scale == 1 ? bytes : (bytes + (scale / 2ULL)) / scale;
    snprintf(out, out_size, "%llu%s", static_cast<unsigned long long>(value), unit);
}

bool parse_hex_byte(const char* text, uint8_t& value)
{
    char*               end    = nullptr;
    const unsigned long parsed = strtoul(text, &end, 16);
    if (end != text + 2 || parsed > 0xff)
    {
        return false;
    }
    value = static_cast<uint8_t>(parsed);
    return true;
}

bool parse_mac(const char* mac, esp_bd_addr_t out)
{
    if (!mac || strlen(mac) != 17)
    {
        return false;
    }

    for (int i = 0; i < ESP_BD_ADDR_LEN; ++i)
    {
        if (!isxdigit(static_cast<unsigned char>(mac[i * 3])) || !isxdigit(static_cast<unsigned char>(mac[i * 3 + 1])) || (i < ESP_BD_ADDR_LEN - 1 && mac[i * 3 + 2] != ':'))
        {
            return false;
        }
        if (!parse_hex_byte(mac + (i * 3), out[i]))
        {
            return false;
        }
    }

    return true;
}

bool parse_datetime(const char* text, m5::rtc_datetime_t& out)
{
    out = {};
    if (!text)
    {
        return false;
    }

    char   digits[15] = {};
    size_t count      = 0;
    for (const char* cursor = text; *cursor && count < sizeof(digits) - 1; ++cursor)
    {
        if (isdigit(static_cast<unsigned char>(*cursor)))
        {
            digits[count++] = *cursor;
        }
    }

    if (count < 4)
    {
        return false;
    }

    const int32_t year    = parse_digits(digits, 0, 4, 0);
    const int32_t month   = count >= 6 ? parse_digits(digits, 4, 2, 1) : 1;
    const int32_t day     = count >= 8 ? parse_digits(digits, 6, 2, 1) : 1;
    const int32_t hours   = count >= 10 ? parse_digits(digits, 8, 2, 0) : 0;
    const int32_t minutes = count >= 12 ? parse_digits(digits, 10, 2, 0) : 0;
    const int32_t seconds = count >= 14 ? parse_digits(digits, 12, 2, 0) : 0;

    if (!valid_date(year, month, day) || !valid_time(hours, minutes, seconds))
    {
        return false;
    }

    out.date.year    = static_cast<int16_t>(year);
    out.date.month   = static_cast<int8_t>(month);
    out.date.date    = static_cast<int8_t>(day);
    out.date.weekDay = weekday_from_days(days_from_civil(year, month, day));
    out.time.hours   = static_cast<int8_t>(hours);
    out.time.minutes = static_cast<int8_t>(minutes);
    out.time.seconds = static_cast<int8_t>(seconds);
    return true;
}

const char* memo_type_to_string(MemoType type)
{
    switch (type)
    {
    case MEMO_TYPE_NOTE:
        return "note";
    case MEMO_TYPE_TODO:
        return "todo";
    case MEMO_TYPE_JOURNAL:
        return "journal";
    case MEMO_TYPE_IDEA:
        return "idea";
    case MEMO_TYPE_REMINDER:
        return "reminder";
    default:
        return "unknown";
    }
}

size_t upsample_s16_mono_2x_duplicate(const int16_t* src, size_t src_samples, int16_t* dst)
{
    if (!src || !dst)
    {
        return 0;
    }

    for (size_t i = 0; i < src_samples; ++i)
    {
        const int16_t sample = src[i];
        dst[(i << 1)]        = sample;
        dst[(i << 1) + 1]    = sample;
    }

    return src_samples << 1;
}

size_t upsample_s16_mono_2x_linear(const int16_t* src, size_t src_samples, int16_t* dst, Upsample2xLinearState& state)
{
    if (!src || !dst)
    {
        return 0;
    }

    size_t  out      = 0;
    int32_t prev     = state.prev;
    bool    has_prev = state.has_prev;

    for (size_t i = 0; i < src_samples; ++i)
    {
        const int32_t sample = src[i];
        dst[out++]           = has_prev ? static_cast<int16_t>((prev + sample) >> 1) : static_cast<int16_t>(sample);
        dst[out++]           = static_cast<int16_t>(sample);
        prev                 = sample;
        has_prev             = true;
    }

    state.prev     = static_cast<int16_t>(prev);
    state.has_prev = has_prev;
    return out;
}

void reset_upsample_2x_linear_state(Upsample2xLinearState& state)
{
    state.has_prev = false;
    state.prev     = 0;
}

size_t mono_s16_to_stereo_s16(const int16_t* src, size_t src_samples, int16_t* dst)
{
    if (!src || !dst)
    {
        return 0;
    }

    const int16_t* src_end = src + src_samples;
    while (src < src_end)
    {
        const int16_t sample = *src++;
        *dst++               = sample;
        *dst++               = sample;
    }

    return src_samples << 1;
}

void requestRebootWithFlag(uint32_t flag)
{
    reset_magic = RESET_MAGIC;
    reset_flag  = flag;

    delay(10);     // let stores settle; tiny paranoia tax
    ESP.restart(); // whole-chip software reset
}

uint32_t generate_8_digit_nonce()
{
    while (true)
    {
        uint32_t x = esp_random();
        x %= 100000000;

        uint32_t hash_input = x >> 8;
        uint8_t  hash       = 0xA5;
        for (uint8_t i = 0; i < 3; ++i)
        {
            hash ^= static_cast<uint8_t>(hash_input);
            hash = static_cast<uint8_t>((hash * 33U) ^ (hash >> 3));
            hash_input >>= 8;
        }

        x &= 0xFFFFFF00;
        x |= hash;
        if (x < 100000000)
        {
            return x;
        }
    }
}
