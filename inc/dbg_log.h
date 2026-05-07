#pragma once

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

#if DBG_LOG_USE_ESP_IDF
#include "esp_log.h"

#define DBG_LOGE(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)
#define DBG_LOGW(tag, format, ...) ESP_LOGW(tag, format, ##__VA_ARGS__)
#define DBG_LOGI(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#define DBG_LOGD(tag, format, ...) ESP_LOGD(tag, format, ##__VA_ARGS__)
#define DBG_LOGV(tag, format, ...) ESP_LOGV(tag, format, ##__VA_ARGS__)

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

#define DBG_LOG_LEVEL(         level, tag, format, ...)         ESP_LOG_LEVEL(         level, tag, format, ##__VA_ARGS__)
#define DBG_LOG_LEVEL_LOCAL(   level, tag, format, ...)         ESP_LOG_LEVEL_LOCAL(   level, tag, format, ##__VA_ARGS__)
#define DBG_LOG_BUFFER_HEX(    tag, buffer, buff_len)           ESP_LOG_BUFFER_HEX(    tag, buffer, buff_len)
#define DBG_LOG_BUFFER_CHAR(   tag, buffer, buff_len)           ESP_LOG_BUFFER_CHAR(   tag, buffer, buff_len)
#define DBG_LOG_BUFFER_HEXDUMP(tag, buffer, buff_len, level)    ESP_LOG_BUFFER_HEXDUMP(tag, buffer, buff_len, level)

#else

#define DBG_LOGE(tag, format, ...) ((void)0)
#define DBG_LOGW(tag, format, ...) ((void)0)
#define DBG_LOGI(tag, format, ...) ((void)0)
#define DBG_LOGD(tag, format, ...) ((void)0)
#define DBG_LOGV(tag, format, ...) ((void)0)

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

#define DBG_LOG_LEVEL         (level, tag, format, ...) ((void)0)
#define DBG_LOG_LEVEL_LOCAL   (level, tag, format, ...) ((void)0)
#define DBG_LOG_BUFFER_HEX    (tag, buffer, buff_len)   ((void)0)
#define DBG_LOG_BUFFER_CHAR   (tag, buffer, buff_len)   ((void)0)
#define DBG_LOG_BUFFER_HEXDUMP(tag, buffer, buff_len, level) ((void)0)

#endif
