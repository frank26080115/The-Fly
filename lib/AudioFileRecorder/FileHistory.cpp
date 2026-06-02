/*
This is used for cloud uploading, a history database is kept noting when a file has been uploaded to the cloud
so that it is not repeatedly uploaded
There's a prune function here that will delete files that have been uploaded according to the history file
*/

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "FileHistory.h"

#include <M5Unified.h>
#include <SdFat.h>
#include <mutex>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ClockAgent.h"
#include "DiskStats.h"
#include "MicroSdCard.h"
#include "dbg_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "utilfuncs.h"

namespace AudioFileRecorder
{
namespace
{

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

constexpr const char* TAG                      = "FileHistory";
constexpr const char* kCloudHistoryPath        = "/cloud_history.txt";
constexpr const char* kCloudHistoryOldPath     = "/cloud_history.old.txt";
constexpr int64_t     kHistoryRetentionSeconds = 3LL * 24LL * 60LL * 60LL;
constexpr size_t      kHistoryLineMaxLength    = 512;
constexpr uint32_t    kPruneHistoryStackBytes  = 6144;

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

std::mutex           g_prune_mutex;
PruneHistoryStatus   g_prune_status   = {};
PruneHistoryCallback g_prune_callback = nullptr;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

void        prune_history_task(void*);
void        finish_prune_history(PruneHistoryState state, PruneHistoryError error);
void        update_prune_status_counts(uint32_t lines_processed,
                                       uint32_t lines_kept,
                                       uint32_t files_pruned,
                                       uint32_t messages_pruned,
                                       uint32_t delete_failures);
bool        datetime_is_older_than_retention(const m5::rtc_datetime_t& datetime, int64_t now_epoch);
int64_t     datetime_to_epoch_seconds(const m5::rtc_datetime_t& datetime);
int64_t     days_from_civil(int32_t year, int32_t month, int32_t day);
bool        read_history_line(FsFile& file, char* line, size_t line_size);
bool        write_history_line(FsFile& file, const char* line);
bool        extract_history_file_path(const char* line, char* path, size_t path_size);
const char* basename_for_path(const char* path);

} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

bool prune_history()
{
#ifdef BUILD_CLOUD_FEATURES
    {
        std::lock_guard<std::mutex> lock(g_prune_mutex);
        if (g_prune_status.state == PruneHistoryState::Busy)
        {
            return false;
        }

        g_prune_status          = {};
        g_prune_status.state    = PruneHistoryState::Busy;
        g_prune_status.error    = PruneHistoryError::None;
        g_prune_status.finished = false;
    }

    const BaseType_t created = xTaskCreatePinnedToCore(prune_history_task,
                                                       "PruneHistory",
                                                       kPruneHistoryStackBytes,
                                                       nullptr,
                                                       1,
                                                       nullptr,
                                                       xPortGetCoreID());
    if (created != pdPASS)
    {
        PruneHistoryCallback callback = nullptr;
        PruneHistoryStatus   status   = {};
        {
            std::lock_guard<std::mutex> lock(g_prune_mutex);
            g_prune_status.state    = PruneHistoryState::Error;
            g_prune_status.error    = PruneHistoryError::TaskCreateFailed;
            g_prune_status.finished = true;
            callback                = g_prune_callback;
            status                  = g_prune_status;
        }
        DBG_LOGE(TAG, "cloud history prune failed: could not create task");
        if (callback)
        {
            callback(status);
        }
        return false;
    }
#else
    PruneHistoryCallback callback = nullptr;
    PruneHistoryStatus   status   = {};
    {
        std::lock_guard<std::mutex> lock(g_prune_mutex);
        g_prune_status          = {};
        g_prune_status.state    = PruneHistoryState::Done;
        g_prune_status.error    = PruneHistoryError::None;
        g_prune_status.finished = true;
        callback                = g_prune_callback;
        status                  = g_prune_status;
    }
    if (callback)
    {
        callback(status);
    }
#endif

    return true;
}

PruneHistoryStatus prune_history_status()
{
    std::lock_guard<std::mutex> lock(g_prune_mutex);
    return g_prune_status;
}

void set_prune_history_complete_callback(PruneHistoryCallback callback)
{
    std::lock_guard<std::mutex> lock(g_prune_mutex);
    g_prune_callback = callback;
}

namespace
{

// -----------------------------------------------------------------------------
// Feature Logic
// -----------------------------------------------------------------------------

void prune_history_task(void*)
{
    if (!MicroSdCard::isReady())
    {
        finish_prune_history(PruneHistoryState::Error, PruneHistoryError::SdNotReady);
        vTaskDelete(nullptr);
        return;
    }

    SdFs&              fs          = MicroSdCard::fs();
    const bool         had_history = fs.exists(kCloudHistoryPath);
    m5::rtc_datetime_t now         = {};
    int64_t            now_epoch   = 0;
    if (had_history)
    {
        if (!Clock.getDateTime(&now))
        {
            finish_prune_history(PruneHistoryState::Error, PruneHistoryError::ClockReadFailed);
            vTaskDelete(nullptr);
            return;
        }
        now_epoch = datetime_to_epoch_seconds(now);
    }

    if (fs.exists(kCloudHistoryOldPath) && !fs.remove(kCloudHistoryOldPath))
    {
        finish_prune_history(PruneHistoryState::Error, PruneHistoryError::FileRotateFailed);
        vTaskDelete(nullptr);
        return;
    }

    if (had_history && !fs.rename(kCloudHistoryPath, kCloudHistoryOldPath))
    {
        finish_prune_history(PruneHistoryState::Error, PruneHistoryError::FileRotateFailed);
        vTaskDelete(nullptr);
        return;
    }

    FsFile old_history;
    if (had_history && !old_history.open(kCloudHistoryOldPath, O_RDONLY))
    {
        finish_prune_history(PruneHistoryState::Error, PruneHistoryError::FileOpenFailed);
        vTaskDelete(nullptr);
        return;
    }

    FsFile new_history;
    if (!new_history.open(kCloudHistoryPath, O_WRONLY | O_CREAT | O_TRUNC))
    {
        if (old_history)
        {
            old_history.close();
        }
        finish_prune_history(PruneHistoryState::Error, PruneHistoryError::FileOpenFailed);
        vTaskDelete(nullptr);
        return;
    }

    uint32_t lines_processed = 0;
    uint32_t lines_kept      = 0;
    uint32_t files_pruned    = 0;
    uint32_t messages_pruned = 0;
    uint32_t delete_failures = 0;
    bool     write_failed    = false;

    char line[kHistoryLineMaxLength];
    while (had_history && read_history_line(old_history, line, sizeof(line)))
    {
        ++lines_processed;

        bool keep_line      = true;
        char file_path[192] = {};
        if (extract_history_file_path(line, file_path, sizeof(file_path)))
        {
            m5::rtc_datetime_t file_time = {};
            if (parse_datetime(basename_for_path(file_path), file_time) &&
                datetime_is_older_than_retention(file_time, now_epoch))
            {
                if (!fs.exists(file_path) || fs.remove(file_path))
                {
                    keep_line = false;
                    ++files_pruned;
                }
                else
                {
                    ++delete_failures;
                }
            }
        }
        else
        {
            m5::rtc_datetime_t message_time = {};
            if (parse_datetime(line, message_time) && datetime_is_older_than_retention(message_time, now_epoch))
            {
                keep_line = false;
                ++messages_pruned;
            }
        }

        if (keep_line)
        {
            if (!write_history_line(new_history, line))
            {
                write_failed = true;
                break;
            }
            ++lines_kept;
        }

        update_prune_status_counts(lines_processed, lines_kept, files_pruned, messages_pruned, delete_failures);
    }

    const bool synced = new_history.sync();
    new_history.close();
    if (old_history)
    {
        old_history.close();
    }

    update_prune_status_counts(lines_processed, lines_kept, files_pruned, messages_pruned, delete_failures);
    if (write_failed || !synced)
    {
        finish_prune_history(PruneHistoryState::Error, PruneHistoryError::FileWriteFailed);
        vTaskDelete(nullptr);
        return;
    }

    finish_prune_history(PruneHistoryState::Done, PruneHistoryError::None);

    vTaskDelete(nullptr);
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

void finish_prune_history(PruneHistoryState state, PruneHistoryError error)
{
    PruneHistoryCallback callback = nullptr;
    PruneHistoryStatus   status   = {};
    {
        std::lock_guard<std::mutex> lock(g_prune_mutex);
        g_prune_status.state    = state;
        g_prune_status.error    = error;
        g_prune_status.finished = true;
        callback                = g_prune_callback;
        status                  = g_prune_status;
    }

    if (state == PruneHistoryState::Error)
    {
        DBG_LOGE(TAG, "cloud history prune failed: error=%d", static_cast<int>(error));
    }
    else
    {
        DBG_LOGI(TAG,
                 "cloud history prune complete: processed=%lu kept=%lu files=%lu messages=%lu delete_failures=%lu",
                 static_cast<unsigned long>(status.lines_processed),
                 static_cast<unsigned long>(status.lines_kept),
                 static_cast<unsigned long>(status.files_pruned),
                 static_cast<unsigned long>(status.messages_pruned),
                 static_cast<unsigned long>(status.delete_failures));
    }

    DiskStats::refreshDiskSpace();

    if (callback)
    {
        callback(status);
    }
}

void update_prune_status_counts(uint32_t lines_processed,
                                uint32_t lines_kept,
                                uint32_t files_pruned,
                                uint32_t messages_pruned,
                                uint32_t delete_failures)
{
    std::lock_guard<std::mutex> lock(g_prune_mutex);
    g_prune_status.lines_processed = lines_processed;
    g_prune_status.lines_kept      = lines_kept;
    g_prune_status.files_pruned    = files_pruned;
    g_prune_status.messages_pruned = messages_pruned;
    g_prune_status.delete_failures = delete_failures;
}

bool datetime_is_older_than_retention(const m5::rtc_datetime_t& datetime, int64_t now_epoch)
{
    return now_epoch - datetime_to_epoch_seconds(datetime) > kHistoryRetentionSeconds;
}

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

int64_t datetime_to_epoch_seconds(const m5::rtc_datetime_t& datetime)
{
    return days_from_civil(datetime.date.year, datetime.date.month, datetime.date.date) * 86400LL +
           static_cast<int64_t>(datetime.time.hours) * 3600LL + static_cast<int64_t>(datetime.time.minutes) * 60LL +
           static_cast<int64_t>(datetime.time.seconds);
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

bool read_history_line(FsFile& file, char* line, size_t line_size)
{
    if (!line || line_size == 0)
    {
        return false;
    }

    bool   saw_any = false;
    size_t count   = 0;
    while (true)
    {
        const int value = file.read();
        if (value < 0)
        {
            break;
        }

        saw_any = true;
        if (value == '\n')
        {
            break;
        }

        if (count + 1 < line_size && value != '\r')
        {
            line[count++] = static_cast<char>(value);
        }
    }

    line[count] = '\0';
    return saw_any;
}

bool write_history_line(FsFile& file, const char* line)
{
    return file.println(line ? line : "") > 0;
}

bool extract_history_file_path(const char* line, char* path, size_t path_size)
{
    if (!line || !path || path_size == 0)
    {
        return false;
    }

    const char* separator = strchr(line, ';');
    if (!separator || separator == line)
    {
        path[0] = '\0';
        return false;
    }

    const size_t length = static_cast<size_t>(separator - line);
    if (length >= path_size)
    {
        path[0] = '\0';
        return false;
    }

    memcpy(path, line, length);
    path[length] = '\0';
    return true;
}

const char* basename_for_path(const char* path)
{
    if (!path)
    {
        return "";
    }

    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

} // namespace

} // namespace AudioFileRecorder
