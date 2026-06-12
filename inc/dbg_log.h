#pragma once

#define DBG_LOG_NONE    0
#define DBG_LOG_ERROR   1
#define DBG_LOG_WARN    2
#define DBG_LOG_INFO    3
#define DBG_LOG_DEBUG   4
#define DBG_LOG_VERBOSE 5

#ifndef DBG_LOG_USE_SERIAL
#define DBG_LOG_USE_SERIAL 0
#endif

#if DBG_LOG_USE_SERIAL
#ifndef DBG_LOG_USE_ESP_IDF
#define DBG_LOG_USE_ESP_IDF 0
#endif
#endif

#ifndef DBG_LOG_USE_ESP_IDF
#if defined(ESP_PLATFORM) || defined(IDF_VER) || defined(CONFIG_IDF_TARGET)
#define DBG_LOG_USE_ESP_IDF 1
#elif defined(__has_include)
#if __has_include("esp_log.h")
#define DBG_LOG_USE_ESP_IDF 1
#else
#define DBG_LOG_USE_ESP_IDF 0
#endif
#else
#define DBG_LOG_USE_ESP_IDF 0
#endif
#endif

#ifndef DBG_LOG_LOCAL_LEVEL
#if defined(LOG_LOCAL_LEVEL)
#define DBG_LOG_LOCAL_LEVEL LOG_LOCAL_LEVEL
#elif defined(CORE_DEBUG_LEVEL)
#define DBG_LOG_LOCAL_LEVEL CORE_DEBUG_LEVEL
#else
#define DBG_LOG_LOCAL_LEVEL DBG_LOG_INFO
#endif
#endif

#if DBG_LOG_USE_SERIAL

#include <Arduino.h>
#include <stdarg.h>

namespace dbg_log_detail
{
inline bool serialLevelEnabled(int level)
{
    return static_cast<int>(DBG_LOG_LOCAL_LEVEL) >= level;
}

inline void serialWrite(char level, const char* tag, const char* format, ...)
{
    Serial.printf("%c (%lu) %s: ", level, static_cast<unsigned long>(millis()), tag ? tag : "");
    va_list args;
    va_start(args, format);
    Serial.vprintf(format, args);
    va_end(args);
    Serial.print("\r\n");
}
} // namespace dbg_log_detail

#define DBG_LOG_SERIAL(level, letter, tag, format, ...) \
    do { \
        if (dbg_log_detail::serialLevelEnabled(level)) { \
            dbg_log_detail::serialWrite(letter, tag, format, ##__VA_ARGS__); \
        } \
    } while (0)

#define DBG_LOG_SERIAL_FORCE(letter, tag, format, ...) \
    do { \
        dbg_log_detail::serialWrite(letter, tag, format, ##__VA_ARGS__); \
    } while (0)

#define DBG_LOGE(tag, format, ...) DBG_LOG_SERIAL(DBG_LOG_ERROR,   'E', tag, format, ##__VA_ARGS__)
#define DBG_LOGW(tag, format, ...) DBG_LOG_SERIAL(DBG_LOG_WARN,    'W', tag, format, ##__VA_ARGS__)
#define DBG_LOGI(tag, format, ...) DBG_LOG_SERIAL(DBG_LOG_INFO,    'I', tag, format, ##__VA_ARGS__)
#define DBG_LOGD(tag, format, ...) DBG_LOG_SERIAL(DBG_LOG_DEBUG,   'D', tag, format, ##__VA_ARGS__)
#define DBG_LOGV(tag, format, ...) DBG_LOG_SERIAL(DBG_LOG_VERBOSE, 'V', tag, format, ##__VA_ARGS__)

#define DBG_LOG_FORCEE(tag, format, ...) DBG_LOG_SERIAL_FORCE('E', tag, format, ##__VA_ARGS__)
#define DBG_LOG_FORCEW(tag, format, ...) DBG_LOG_SERIAL_FORCE('W', tag, format, ##__VA_ARGS__)
#define DBG_LOG_FORCEI(tag, format, ...) DBG_LOG_SERIAL_FORCE('I', tag, format, ##__VA_ARGS__)
#define DBG_LOG_FORCED(tag, format, ...) DBG_LOG_SERIAL_FORCE('D', tag, format, ##__VA_ARGS__)
#define DBG_LOG_FORCEV(tag, format, ...) DBG_LOG_SERIAL_FORCE('V', tag, format, ##__VA_ARGS__)

#define DBG_EARLY_LOGE(tag, format, ...) DBG_LOGE(tag, format, ##__VA_ARGS__)
#define DBG_EARLY_LOGW(tag, format, ...) DBG_LOGW(tag, format, ##__VA_ARGS__)
#define DBG_EARLY_LOGI(tag, format, ...) DBG_LOGI(tag, format, ##__VA_ARGS__)
#define DBG_EARLY_LOGD(tag, format, ...) DBG_LOGD(tag, format, ##__VA_ARGS__)
#define DBG_EARLY_LOGV(tag, format, ...) DBG_LOGV(tag, format, ##__VA_ARGS__)

#define DBG_DRAM_LOGE(tag, format, ...) DBG_LOGE(tag, format, ##__VA_ARGS__)
#define DBG_DRAM_LOGW(tag, format, ...) DBG_LOGW(tag, format, ##__VA_ARGS__)
#define DBG_DRAM_LOGI(tag, format, ...) DBG_LOGI(tag, format, ##__VA_ARGS__)
#define DBG_DRAM_LOGD(tag, format, ...) DBG_LOGD(tag, format, ##__VA_ARGS__)
#define DBG_DRAM_LOGV(tag, format, ...) DBG_LOGV(tag, format, ##__VA_ARGS__)

#define DBG_LOG_LEVEL(level, tag, format, ...) DBG_LOG_SERIAL(level, '?', tag, format, ##__VA_ARGS__)
#define DBG_LOG_LEVEL_LOCAL(level, tag, format, ...) DBG_LOG_LEVEL(level, tag, format, ##__VA_ARGS__)
#define DBG_LOG_BUFFER_HEX(tag, buffer, buff_len) ((void)0)
#define DBG_LOG_BUFFER_CHAR(tag, buffer, buff_len) ((void)0)
#define DBG_LOG_BUFFER_HEXDUMP(tag, buffer, buff_len, level) ((void)0)

#elif DBG_LOG_USE_ESP_IDF

#include "esp_log.h"

#define DBG_LOGE(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)
#define DBG_LOGW(tag, format, ...) ESP_LOGW(tag, format, ##__VA_ARGS__)
#define DBG_LOGI(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#define DBG_LOGD(tag, format, ...) ESP_LOGD(tag, format, ##__VA_ARGS__)
#define DBG_LOGV(tag, format, ...) ESP_LOGV(tag, format, ##__VA_ARGS__)

#define DBG_LOG_FORCEE(tag, format, ...) ESP_LOG_LEVEL(ESP_LOG_ERROR,   tag, format, ##__VA_ARGS__)
#define DBG_LOG_FORCEW(tag, format, ...) ESP_LOG_LEVEL(ESP_LOG_WARN,    tag, format, ##__VA_ARGS__)
#define DBG_LOG_FORCEI(tag, format, ...) ESP_LOG_LEVEL(ESP_LOG_INFO,    tag, format, ##__VA_ARGS__)
#define DBG_LOG_FORCED(tag, format, ...) ESP_LOG_LEVEL(ESP_LOG_DEBUG,   tag, format, ##__VA_ARGS__)
#define DBG_LOG_FORCEV(tag, format, ...) ESP_LOG_LEVEL(ESP_LOG_VERBOSE, tag, format, ##__VA_ARGS__)

#define DBG_EARLY_LOGE(tag, format, ...) ESP_EARLY_LOGE(tag, format, ##__VA_ARGS__)
#define DBG_EARLY_LOGW(tag, format, ...) ESP_EARLY_LOGW(tag, format, ##__VA_ARGS__)
#define DBG_EARLY_LOGI(tag, format, ...) ESP_EARLY_LOGI(tag, format, ##__VA_ARGS__)
#define DBG_EARLY_LOGD(tag, format, ...) ESP_EARLY_LOGD(tag, format, ##__VA_ARGS__)
#define DBG_EARLY_LOGV(tag, format, ...) ESP_EARLY_LOGV(tag, format, ##__VA_ARGS__)

#define DBG_DRAM_LOGE(tag, format, ...) ESP_DRAM_LOGE(tag, format, ##__VA_ARGS__)
#define DBG_DRAM_LOGW(tag, format, ...) ESP_DRAM_LOGW(tag, format, ##__VA_ARGS__)
#define DBG_DRAM_LOGI(tag, format, ...) ESP_DRAM_LOGI(tag, format, ##__VA_ARGS__)
#define DBG_DRAM_LOGD(tag, format, ...) ESP_DRAM_LOGD(tag, format, ##__VA_ARGS__)
#define DBG_DRAM_LOGV(tag, format, ...) ESP_DRAM_LOGV(tag, format, ##__VA_ARGS__)

#define DBG_LOG_LEVEL(level, tag, format, ...) ESP_LOG_LEVEL(level, tag, format, ##__VA_ARGS__)
#define DBG_LOG_LEVEL_LOCAL(level, tag, format, ...) ESP_LOG_LEVEL_LOCAL(level, tag, format, ##__VA_ARGS__)
#define DBG_LOG_BUFFER_HEX(tag, buffer, buff_len) ESP_LOG_BUFFER_HEX(tag, buffer, buff_len)
#define DBG_LOG_BUFFER_CHAR(tag, buffer, buff_len) ESP_LOG_BUFFER_CHAR(tag, buffer, buff_len)
#define DBG_LOG_BUFFER_HEXDUMP(tag, buffer, buff_len, level) ESP_LOG_BUFFER_HEXDUMP(tag, buffer, buff_len, level)

#else

#define DBG_LOGE(tag, format, ...) ((void)0)
#define DBG_LOGW(tag, format, ...) ((void)0)
#define DBG_LOGI(tag, format, ...) ((void)0)
#define DBG_LOGD(tag, format, ...) ((void)0)
#define DBG_LOGV(tag, format, ...) ((void)0)

#define DBG_LOG_FORCEE(tag, format, ...) ((void)0)
#define DBG_LOG_FORCEW(tag, format, ...) ((void)0)
#define DBG_LOG_FORCEI(tag, format, ...) ((void)0)
#define DBG_LOG_FORCED(tag, format, ...) ((void)0)
#define DBG_LOG_FORCEV(tag, format, ...) ((void)0)

#define DBG_EARLY_LOGE(tag, format, ...) ((void)0)
#define DBG_EARLY_LOGW(tag, format, ...) ((void)0)
#define DBG_EARLY_LOGI(tag, format, ...) ((void)0)
#define DBG_EARLY_LOGD(tag, format, ...) ((void)0)
#define DBG_EARLY_LOGV(tag, format, ...) ((void)0)

#define DBG_DRAM_LOGE(tag, format, ...) ((void)0)
#define DBG_DRAM_LOGW(tag, format, ...) ((void)0)
#define DBG_DRAM_LOGI(tag, format, ...) ((void)0)
#define DBG_DRAM_LOGD(tag, format, ...) ((void)0)
#define DBG_DRAM_LOGV(tag, format, ...) ((void)0)

#define DBG_LOG_LEVEL(level, tag, format, ...) ((void)0)
#define DBG_LOG_LEVEL_LOCAL(level, tag, format, ...) ((void)0)
#define DBG_LOG_BUFFER_HEX(tag, buffer, buff_len) ((void)0)
#define DBG_LOG_BUFFER_CHAR(tag, buffer, buff_len) ((void)0)
#define DBG_LOG_BUFFER_HEXDUMP(tag, buffer, buff_len, level) ((void)0)

#endif
