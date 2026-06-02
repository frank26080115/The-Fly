/*
The purpose of this module is handling many methods of providing disk statistics
Especially since calculating the free disk space takes a long time, such data is cached
*/

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "DiskStats.h"

#include <SdFat.h>
#include <ctype.h>
#include <mutex>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Display.h"
#include "FlyGui.h"
#include "MicroSdCard.h"

namespace DiskStats
{
namespace
{

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

constexpr const char* kCloudHistoryPath     = "/cloud_history.txt";
constexpr const char* kFirmwareUpdatePath   = "/firmware.bin";
constexpr size_t      kPathMaxLength        = 192;
constexpr size_t      kFileNameMaxLength    = 64;
constexpr size_t      kDateTimeMaxLength    = 24;
constexpr size_t      kHistoryLineMaxLength = kPathMaxLength + kDateTimeMaxLength + 8;
constexpr uint64_t    kBytesPerGiB          = 1024ULL * 1024ULL * 1024ULL;
constexpr uint64_t    kLowDiskBytes         = 4ULL * kBytesPerGiB;
constexpr uint64_t    kFullDiskBytes        = 2ULL * kBytesPerGiB;
constexpr int16_t     kDiskWarningX         = 160;
constexpr int16_t     kDiskWarningY         = 1;
constexpr int16_t     kDiskWarningWidth     = 72;

// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------

struct UploadedPathNode
{
    char              path[kPathMaxLength] = {};
    UploadedPathNode* next                 = nullptr;
};

struct RecordingStats
{
    uint32_t total_rec_files                               = 0;
    uint32_t total_rec_files_not_uploaded                  = 0;
    char     last_upload_datetime[kDateTimeMaxLength]      = {};
    char     latest_recorded_file_name[kFileNameMaxLength] = {};
};

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

std::mutex g_mutex;
uint64_t   g_total_disk_space                              = 0;
uint64_t   g_free_disk_space                               = 0;
bool       g_disk_space_valid                              = false;
bool       g_firmware_update_file_exists                   = false;
uint32_t   g_total_rec_files                               = 0;
uint32_t   g_total_rec_files_not_uploaded                  = 0;
char       g_last_upload_datetime[kDateTimeMaxLength]      = {};
char       g_latest_recorded_file_name[kFileNameMaxLength] = {};

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

bool        load_uploaded_paths(UploadedPathNode*& head, UploadedPathNode*& tail, RecordingStats& stats);
bool        scan_recording_directory(const char*             path,
                                     const UploadedPathNode* uploaded_paths,
                                     RecordingStats&         stats,
                                     uint64_t&               latest_key);
bool        firmware_update_file_exists();
void        draw_disk_space_warning(bool valid, uint64_t free_bytes);
void        update_latest_recording(RecordingStats& stats, const char* name, uint64_t key, uint64_t& latest_key);
bool        append_uploaded_path(UploadedPathNode*& head, UploadedPathNode*& tail, const char* path);
void        free_uploaded_paths(UploadedPathNode* head);
bool        uploaded_path_contains(const UploadedPathNode* head, const char* path);
bool        extract_success_history_path(const char* line, char* path, size_t path_size, const char*& datetime);
bool        read_fs_line(FsFile& file, char* line, size_t line_size);
bool        make_child_path(const char* parent, const char* child, char* out, size_t out_size);
bool        is_recording_path(const char* path);
bool        ends_with_extension(const char* path, const char* extension);
const char* basename_for_path(const char* path);
uint64_t    filename_datetime_key(const char* name);
uint64_t    fat_datetime_key(FsFile& file);
uint64_t
decimal_datetime_key(uint32_t year, uint32_t month, uint32_t day, uint32_t hours, uint32_t minutes, uint32_t seconds);
uint32_t parse_digits(const char* text, size_t offset, size_t count);
uint8_t  percent_u8(uint64_t numerator, uint64_t denominator);

} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

bool refreshDiskSpace()
{
    uint64_t   total           = 0;
    uint64_t   free            = 0;
    bool       firmware_exists = false;
    const bool ok              = MicroSdCard::isReady();
    if (ok)
    {
        total           = MicroSdCard::totalBytes();
        free            = MicroSdCard::freeBytes();
        firmware_exists = firmware_update_file_exists();
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    g_total_disk_space            = total;
    g_free_disk_space             = free;
    g_disk_space_valid            = ok;
    g_firmware_update_file_exists = firmware_exists;
    draw_disk_space_warning(g_disk_space_valid, g_free_disk_space);
    return ok;
}

bool refreshRecordingUploadStats()
{
    RecordingStats    stats           = {};
    UploadedPathNode* uploaded_head   = nullptr;
    UploadedPathNode* uploaded_tail   = nullptr;
    uint64_t          latest_key      = 0;
    bool              firmware_exists = false;

    bool ok = MicroSdCard::isReady();
    if (ok)
    {
        firmware_exists = firmware_update_file_exists();
#ifdef BUILD_CLOUD_FEATURES
        ok = load_uploaded_paths(uploaded_head, uploaded_tail, stats) &&
             scan_recording_directory("/", uploaded_head, stats, latest_key);
#else
        ok                                 = scan_recording_directory("/", nullptr, stats, latest_key);
        stats.total_rec_files_not_uploaded = 0;
#endif
    }
    free_uploaded_paths(uploaded_head);

    std::lock_guard<std::mutex> lock(g_mutex);
    if (ok)
    {
        g_total_rec_files              = stats.total_rec_files;
        g_total_rec_files_not_uploaded = stats.total_rec_files_not_uploaded;
        strlcpy(g_last_upload_datetime, stats.last_upload_datetime, sizeof(g_last_upload_datetime));
        strlcpy(g_latest_recorded_file_name, stats.latest_recorded_file_name, sizeof(g_latest_recorded_file_name));
    }
    else
    {
        g_total_rec_files              = 0;
        g_total_rec_files_not_uploaded = 0;
        g_last_upload_datetime[0]      = '\0';
        g_latest_recorded_file_name[0] = '\0';
    }
    g_firmware_update_file_exists = firmware_exists;
    return ok;
}

void drawDiskSpaceWarning()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    draw_disk_space_warning(g_disk_space_valid, g_free_disk_space);
}

// -----------------------------------------------------------------------------
// State Accessors
// -----------------------------------------------------------------------------

bool diskSpace(uint64_t& total_bytes, uint64_t& free_bytes)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    total_bytes = g_total_disk_space;
    free_bytes  = g_free_disk_space;
    return g_disk_space_valid;
}

uint64_t totalDiskSpace()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_total_disk_space;
}

uint64_t freeDiskSpace()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_free_disk_space;
}

uint8_t freeDiskSpacePercent()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return percent_u8(g_free_disk_space, g_total_disk_space);
}

bool firmwareUpdateFileExists()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_firmware_update_file_exists;
}

uint32_t totalRecFilesStored()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_total_rec_files;
}

uint32_t totalRecFilesNotUploaded()
{
#ifdef BUILD_CLOUD_FEATURES
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_total_rec_files_not_uploaded;
#else
    return 0;
#endif
}

const char* lastUploadDateTime()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_last_upload_datetime;
}

const char* latestRecordedFileName()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_latest_recorded_file_name;
}

namespace
{

// -----------------------------------------------------------------------------
// Feature Logic
// -----------------------------------------------------------------------------

bool load_uploaded_paths(UploadedPathNode*& head, UploadedPathNode*& tail, RecordingStats& stats)
{
    head = nullptr;
    tail = nullptr;

    FsFile history;
    if (!history.open(kCloudHistoryPath, O_RDONLY))
    {
        return true;
    }

    char line[kHistoryLineMaxLength];
    char path[kPathMaxLength];
    while (read_fs_line(history, line, sizeof(line)))
    {
        const char* datetime = nullptr;
        if (!extract_success_history_path(line, path, sizeof(path), datetime))
        {
            continue;
        }

        if (!append_uploaded_path(head, tail, path))
        {
            history.close();
            free_uploaded_paths(head);
            head = nullptr;
            tail = nullptr;
            return false;
        }

        strlcpy(stats.last_upload_datetime, datetime ? datetime : "", sizeof(stats.last_upload_datetime));
    }

    history.close();
    return true;
}

bool scan_recording_directory(const char*             path,
                              const UploadedPathNode* uploaded_paths,
                              RecordingStats&         stats,
                              uint64_t&               latest_key)
{
    FsFile dir;
    if (!dir.open(path, O_RDONLY))
    {
        return false;
    }

    FsFile child;
    char   child_name[kFileNameMaxLength];
    char   child_path[kPathMaxLength];

    while (child.openNext(&dir, O_RDONLY))
    {
        child.getName(child_name, sizeof(child_name));
        if (child_name[0] == '\0' || !make_child_path(path, child_name, child_path, sizeof(child_path)))
        {
            child.close();
            continue;
        }

        if (child.isDir())
        {
            const bool ok = scan_recording_directory(child_path, uploaded_paths, stats, latest_key);
            child.close();
            if (!ok)
            {
                dir.close();
                return false;
            }
            continue;
        }

        if (child.isFile() && is_recording_path(child_path))
        {
            ++stats.total_rec_files;
            if (!uploaded_path_contains(uploaded_paths, child_path))
            {
                ++stats.total_rec_files_not_uploaded;
            }

            uint64_t key = filename_datetime_key(child_name);
            if (key == 0)
            {
                key = fat_datetime_key(child);
            }
            update_latest_recording(stats, child_name, key, latest_key);
        }

        child.close();
    }

    dir.close();
    return true;
}

bool firmware_update_file_exists()
{
#ifndef TEST_MOCK_FW_UPDATE
    if (!MicroSdCard::isReady())
    {
        return false;
    }

    FsFile file;
    if (!file.open(kFirmwareUpdatePath, O_RDONLY))
    {
        return false;
    }

    const bool exists = file.isFile();
    file.close();
    return exists;
#else
    return true;
#endif
}

void draw_disk_space_warning(bool valid, uint64_t free_bytes)
{
    thefly_display.fillRect(kDiskWarningX, 0, kDiskWarningWidth, FlyGui::kTopBarHeight, TFT_BLACK);

    if (!valid || free_bytes >= kLowDiskBytes)
    {
        return;
    }

    thefly_display.setTextFont(1);
    thefly_display.setTextSize(1.0f);
    thefly_display.setTextDatum(top_left);
    thefly_display.setTextColor(free_bytes < kFullDiskBytes ? TFT_RED : TFT_YELLOW, TFT_BLACK);
    thefly_display.drawString(free_bytes < kFullDiskBytes ? "MEM FULL" : "MEM", kDiskWarningX, kDiskWarningY);
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

void update_latest_recording(RecordingStats& stats, const char* name, uint64_t key, uint64_t& latest_key)
{
    if (!name || name[0] == '\0')
    {
        return;
    }

    if (latest_key == 0 || key > latest_key || (key == latest_key && strcmp(name, stats.latest_recorded_file_name) > 0))
    {
        latest_key = key;
        strlcpy(stats.latest_recorded_file_name, name, sizeof(stats.latest_recorded_file_name));
    }
}

bool append_uploaded_path(UploadedPathNode*& head, UploadedPathNode*& tail, const char* path)
{
    UploadedPathNode* node = static_cast<UploadedPathNode*>(calloc(1, sizeof(UploadedPathNode)));
    if (!node)
    {
        return false;
    }

    strlcpy(node->path, path ? path : "", sizeof(node->path));
    if (tail)
    {
        tail->next = node;
    }
    else
    {
        head = node;
    }
    tail = node;
    return true;
}

void free_uploaded_paths(UploadedPathNode* head)
{
    while (head)
    {
        UploadedPathNode* next = head->next;
        free(head);
        head = next;
    }
}

bool uploaded_path_contains(const UploadedPathNode* head, const char* path)
{
    for (const UploadedPathNode* node = head; node; node = node->next)
    {
        if (strcmp(node->path, path) == 0)
        {
            return true;
        }
    }
    return false;
}

bool extract_success_history_path(const char* line, char* path, size_t path_size, const char*& datetime)
{
    datetime = nullptr;
    if (!line || !path || path_size == 0)
    {
        return false;
    }

    const char* separator = strchr(line, ';');
    if (!separator || separator == line)
    {
        return false;
    }

    const size_t path_length = static_cast<size_t>(separator - line);
    if (path_length >= path_size)
    {
        return false;
    }

    memcpy(path, line, path_length);
    path[path_length] = '\0';
    datetime          = separator + 1;
    return is_recording_path(path);
}

bool read_fs_line(FsFile& file, char* line, size_t line_size)
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

bool make_child_path(const char* parent, const char* child, char* out, size_t out_size)
{
    if (!parent || !child || !out || out_size == 0)
    {
        return false;
    }

    const bool parent_is_root = strcmp(parent, "/") == 0;
    const int  written =
        parent_is_root ? snprintf(out, out_size, "/%s", child) : snprintf(out, out_size, "%s/%s", parent, child);
    return written > 0 && static_cast<size_t>(written) < out_size;
}

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

bool is_recording_path(const char* path)
{
    return ends_with_extension(path, ".rec") || ends_with_extension(path, ".fly") ||
           ends_with_extension(path, ".mp3") || ends_with_extension(path, ".wav");
}

bool ends_with_extension(const char* path, const char* extension)
{
    if (!path || !extension)
    {
        return false;
    }

    const size_t length           = strlen(path);
    const size_t extension_length = strlen(extension);
    if (length < extension_length)
    {
        return false;
    }

    const char* suffix = path + length - extension_length;
    for (size_t i = 0; i < extension_length; ++i)
    {
        if (tolower(static_cast<unsigned char>(suffix[i])) != tolower(static_cast<unsigned char>(extension[i])))
        {
            return false;
        }
    }

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

uint64_t filename_datetime_key(const char* name)
{
    if (!name)
    {
        return 0;
    }

    char   digits[15] = {};
    size_t count      = 0;
    for (const char* cursor = name; *cursor && count < sizeof(digits) - 1; ++cursor)
    {
        if (isdigit(static_cast<unsigned char>(*cursor)))
        {
            digits[count++] = *cursor;
        }
    }

    if (count < 14)
    {
        return 0;
    }

    return decimal_datetime_key(parse_digits(digits, 0, 4),
                                parse_digits(digits, 4, 2),
                                parse_digits(digits, 6, 2),
                                parse_digits(digits, 8, 2),
                                parse_digits(digits, 10, 2),
                                parse_digits(digits, 12, 2));
}

uint64_t fat_datetime_key(FsFile& file)
{
    uint16_t date = 0;
    uint16_t time = 0;
    if (!file.getCreateDateTime(&date, &time))
    {
        return 0;
    }

    const uint32_t year    = 1980U + ((date >> 9) & 0x7fU);
    const uint32_t month   = (date >> 5) & 0x0fU;
    const uint32_t day     = date & 0x1fU;
    const uint32_t hours   = (time >> 11) & 0x1fU;
    const uint32_t minutes = (time >> 5) & 0x3fU;
    const uint32_t seconds = (time & 0x1fU) * 2U;
    return decimal_datetime_key(year, month, day, hours, minutes, seconds);
}

uint64_t
decimal_datetime_key(uint32_t year, uint32_t month, uint32_t day, uint32_t hours, uint32_t minutes, uint32_t seconds)
{
    return static_cast<uint64_t>(year) * 10000000000ULL + static_cast<uint64_t>(month) * 100000000ULL +
           static_cast<uint64_t>(day) * 1000000ULL + static_cast<uint64_t>(hours) * 10000ULL +
           static_cast<uint64_t>(minutes) * 100ULL + static_cast<uint64_t>(seconds);
}

uint32_t parse_digits(const char* text, size_t offset, size_t count)
{
    uint32_t value = 0;
    for (size_t i = 0; i < count; ++i)
    {
        value = value * 10U + static_cast<uint32_t>(text[offset + i] - '0');
    }
    return value;
}

uint8_t percent_u8(uint64_t numerator, uint64_t denominator)
{
    if (denominator == 0)
    {
        return 0;
    }

    uint64_t value = ((numerator * 100ULL) + (denominator / 2ULL)) / denominator;
    if (value > 100ULL)
    {
        value = 100ULL;
    }
    return static_cast<uint8_t>(value);
}

} // namespace

} // namespace DiskStats
