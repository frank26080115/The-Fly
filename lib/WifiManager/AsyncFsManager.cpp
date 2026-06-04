// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "AsyncFsManager.h"

#include <SdFat.h>
#include <string.h>

#include "MicroSdCard.h"
#include "dbg_log.h"

namespace AsyncFsManager
{
namespace
{

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

constexpr const char* TAG                     = "AsyncFsManager";
constexpr size_t      kDownloadPathSize       = 256;
constexpr size_t      kUploadPathSize         = 256;
constexpr uint64_t    kUploadSyncEveryBytes   = 512ULL * 1024ULL;
constexpr uint32_t    kFileActivityGuiQuietMs = 200;

// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------

class Lock
{
public:
    Lock();
    ~Lock();

    bool locked() const;

private:
    bool m_locked = false;
};

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

SemaphoreHandle_t g_mutex = nullptr;
FsFile            g_walk_root;
FsFile            g_walk_child;
FsFile            g_download_file;
FsFile            g_upload_file;
uint64_t          g_download_file_size               = 0;
uint64_t          g_download_position                = 0;
char              g_download_path[kDownloadPathSize] = {};
uint64_t          g_upload_position                  = 0;
char              g_upload_path[kUploadPathSize]     = {};
uint32_t          g_upload_session_id                = 0;
uint32_t          g_next_upload_session_id           = 0;
uint64_t          g_upload_last_sync_position        = 0;
uint32_t          g_last_file_activity_time          = 0;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

bool ensure_mutex();
void note_file_activity();
bool any_file_open_locked();
void close_walk_locked();
void close_download_locked();
void close_upload_locked();
bool upload_session_matches_locked(uint32_t session_id);
bool reset_walk_locked();
bool reopen_download_locked();
int  read_download_locked(uint64_t position, uint8_t* buffer, size_t max_len);

} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

bool init()
{
    Lock lock;
    if (!lock.locked())
    {
        DBG_LOGW(TAG, "reset walk failed: mutex unavailable");
        return false;
    }

    close_walk_locked();
    close_download_locked();
    close_upload_locked();
    g_last_file_activity_time = 0;
    return true;
}

bool isReady()
{
    Lock lock;
    return lock.locked() && MicroSdCard::isReady();
}

bool guiShouldYield()
{
    Lock lock;
    if (!lock.locked())
    {
        return false;
    }
    if (any_file_open_locked())
    {
        return true;
    }
    if (g_last_file_activity_time == 0)
    {
        return false;
    }
    if ((millis() - g_last_file_activity_time) < kFileActivityGuiQuietMs)
    {
        return true;
    }
    g_last_file_activity_time = 0;
    return false;
}

bool resetWalk()
{
    note_file_activity();

    Lock lock;
    if (!lock.locked())
    {
        return false;
    }
    if (g_upload_file.isOpen())
    {
        DBG_LOGW(TAG, "reset walk failed: upload active path=%s", g_upload_path[0] ? g_upload_path : "(empty)");
        return false;
    }
    return lock.locked() && reset_walk_locked();
}

WalkResult walkOne(char* file_name, size_t file_name_size)
{
    note_file_activity();

    if (!file_name || file_name_size == 0)
    {
        return WalkResult::Error;
    }
    file_name[0] = '\0';

    Lock lock;
    if (!lock.locked())
    {
        DBG_LOGW(TAG, "walk one failed: mutex unavailable");
        return WalkResult::Error;
    }
    if (g_upload_file.isOpen())
    {
        DBG_LOGW(TAG, "walk one failed: upload active path=%s", g_upload_path[0] ? g_upload_path : "(empty)");
        return WalkResult::Error;
    }
    if (!MicroSdCard::isReady())
    {
        DBG_LOGW(TAG, "walk one failed: microSD not ready");
        close_walk_locked();
        return WalkResult::NotReady;
    }
    if (!g_walk_root.isOpen() && !reset_walk_locked())
    {
        DBG_LOGW(TAG, "walk one failed: root not open and reset failed");
        return WalkResult::Error;
    }

    while (g_walk_child.openNext(&g_walk_root, O_RDONLY))
    {
        if (!g_walk_child.isFile())
        {
            g_walk_child.close();
            continue;
        }

        g_walk_child.getName(file_name, file_name_size);
        g_walk_child.close();
        if (file_name[0] != '\0')
        {
            return WalkResult::File;
        }
    }

    close_walk_locked();
    return WalkResult::End;
}

void closeWalk()
{
    note_file_activity();

    Lock lock;
    if (lock.locked())
    {
        close_walk_locked();
    }
}

bool openFileForDownload(const char* path, uint64_t* file_size)
{
    note_file_activity();

    if (file_size)
    {
        *file_size = 0;
    }

    Lock lock;
    if (!lock.locked())
    {
        DBG_LOGW(TAG, "open download failed: mutex unavailable path=%s", path ? path : "(null)");
        return false;
    }

    close_walk_locked();
    close_download_locked();
    if (g_upload_file.isOpen())
    {
        DBG_LOGW(TAG, "open download failed: upload active path=%s", g_upload_path[0] ? g_upload_path : "(empty)");
        return false;
    }

    if (!path || path[0] == '\0' || !MicroSdCard::isReady())
    {
        DBG_LOGW(TAG,
                 "open download failed: invalid state path=%s sd_ready=%d",
                 path ? path : "(null)",
                 MicroSdCard::isReady() ? 1 : 0);
        return false;
    }
    if (!g_download_file.open(path, O_RDONLY))
    {
        note_file_activity();
        DBG_LOGW(TAG, "open download failed: could not open path=%s", path);
        close_download_locked();
        return false;
    }
    note_file_activity();
    if (g_download_file.isDir())
    {
        DBG_LOGW(TAG, "open download failed: path is directory path=%s", path);
        close_download_locked();
        return false;
    }
    if (!g_download_file.seekSet(0))
    {
        note_file_activity();
        DBG_LOGW(TAG, "open download failed: seek to start failed path=%s", path);
        close_download_locked();
        return false;
    }
    note_file_activity();

    g_download_file_size = g_download_file.fileSize();
    note_file_activity();
    g_download_position  = 0;
    strlcpy(g_download_path, path, sizeof(g_download_path));
    if (file_size)
    {
        *file_size = g_download_file_size;
    }
    return true;
}

int readFileChunk(uint64_t position, uint8_t* buffer, size_t max_len)
{
    note_file_activity();

    if (!buffer || max_len == 0)
    {
        DBG_LOGW(TAG,
                 "read download failed: invalid buffer=%p max_len=%u pos=%llu",
                 static_cast<void*>(buffer),
                 static_cast<unsigned>(max_len),
                 static_cast<unsigned long long>(position));
        return 0;
    }

    Lock lock;
    if (!lock.locked() || !g_download_file.isOpen())
    {
        DBG_LOGW(TAG,
                 "read download failed: lock=%d open=%d pos=%llu max=%u size=%llu",
                 lock.locked() ? 1 : 0,
                 g_download_file.isOpen() ? 1 : 0,
                 static_cast<unsigned long long>(position),
                 static_cast<unsigned>(max_len),
                 static_cast<unsigned long long>(g_download_file_size));
        return -1;
    }
    if (position >= g_download_file_size)
    {
        DBG_LOGI(TAG,
                 "read download EOF: pos=%llu size=%llu",
                 static_cast<unsigned long long>(position),
                 static_cast<unsigned long long>(g_download_file_size));
        close_download_locked();
        return 0;
    }

    const uint64_t remaining = g_download_file_size - position;
    if (max_len > remaining)
    {
        max_len = static_cast<size_t>(remaining);
    }
    int bytes_read = read_download_locked(position, buffer, max_len);
    if (bytes_read < 0)
    {
        DBG_LOGW(TAG,
                 "read download retrying after failure: path=%s pos=%llu wanted=%u sd_error=0x%02X sd_data=0x%02X",
                 g_download_path[0] ? g_download_path : "(empty)",
                 static_cast<unsigned long long>(position),
                 static_cast<unsigned>(max_len),
                 MicroSdCard::fs().sdErrorCode(),
                 MicroSdCard::fs().sdErrorData());
        if (reopen_download_locked())
        {
            bytes_read = read_download_locked(position, buffer, max_len);
        }
    }
    if (bytes_read <= 0)
    {
        DBG_LOGW(TAG,
                 "read download failed: read returned %d path=%s pos=%llu wanted=%u size=%llu sd_error=0x%02X "
                 "sd_data=0x%02X",
                 bytes_read,
                 g_download_path[0] ? g_download_path : "(empty)",
                 static_cast<unsigned long long>(position),
                 static_cast<unsigned>(max_len),
                 static_cast<unsigned long long>(g_download_file_size),
                 MicroSdCard::fs().sdErrorCode(),
                 MicroSdCard::fs().sdErrorData());
        close_download_locked();
        return bytes_read;
    }
    if (position + static_cast<uint64_t>(bytes_read) >= g_download_file_size)
    {
        DBG_LOGI(TAG,
                 "read download complete: pos=%llu read=%d size=%llu",
                 static_cast<unsigned long long>(position),
                 bytes_read,
                 static_cast<unsigned long long>(g_download_file_size));
        close_download_locked();
    }
    return bytes_read;
}

uint64_t openFileSize()
{
    Lock lock;
    return lock.locked() ? g_download_file_size : 0;
}

void closeFile()
{
    note_file_activity();

    Lock lock;
    if (lock.locked())
    {
        close_download_locked();
    }
}

bool openFileForUpload(const char* path, uint64_t expected_size, uint32_t* session_id)
{
    note_file_activity();

    if (session_id)
    {
        *session_id = 0;
    }

    Lock lock;
    if (!lock.locked() || !path || path[0] == '\0' || !MicroSdCard::isReady())
    {
        DBG_LOGW(TAG,
                 "open upload failed: lock=%d path=%s sd_ready=%d",
                 lock.locked() ? 1 : 0,
                 path ? path : "(null)",
                 MicroSdCard::isReady() ? 1 : 0);
        return false;
    }
    if (g_upload_file.isOpen())
    {
        DBG_LOGW(TAG, "open upload failed: upload already active path=%s", g_upload_path[0] ? g_upload_path : "(empty)");
        return false;
    }

    close_walk_locked();
    close_download_locked();

    if (!g_upload_file.open(path, O_WRONLY | O_CREAT | O_TRUNC))
    {
        note_file_activity();
        DBG_LOGW(TAG,
                 "open upload failed: could not open path=%s sd_error=0x%02X sd_data=0x%02X",
                 path,
                 MicroSdCard::fs().sdErrorCode(),
                 MicroSdCard::fs().sdErrorData());
        close_upload_locked();
        return false;
    }
    note_file_activity();

    if (expected_size > 0)
    {
        if (g_upload_file.preAllocate(expected_size))
        {
            note_file_activity();
            g_upload_file.rewind();
            note_file_activity();
            DBG_LOGI(TAG, "upload preallocated path=%s size=%llu", path, static_cast<unsigned long long>(expected_size));
        }
        else
        {
            note_file_activity();
            DBG_LOGW(TAG,
                     "upload preallocate failed, continuing path=%s size=%llu sd_error=0x%02X sd_data=0x%02X",
                     path,
                     static_cast<unsigned long long>(expected_size),
                     MicroSdCard::fs().sdErrorCode(),
                     MicroSdCard::fs().sdErrorData());
            g_upload_file.rewind();
            note_file_activity();
        }
    }

    g_upload_position = 0;
    g_upload_last_sync_position = 0;
    do
    {
        ++g_next_upload_session_id;
    } while (g_next_upload_session_id == 0);
    g_upload_session_id = g_next_upload_session_id;
    strlcpy(g_upload_path, path, sizeof(g_upload_path));
    if (session_id)
    {
        *session_id = g_upload_session_id;
    }
    return true;
}

bool writeUploadFileChunk(uint32_t session_id, uint64_t position, const uint8_t* data, size_t len)
{
    note_file_activity();

    Lock lock;
    if (!lock.locked() || !g_upload_file.isOpen() || !upload_session_matches_locked(session_id))
    {
        DBG_LOGW(TAG,
                 "write upload failed: lock=%d open=%d session=%lu active=%lu pos=%llu len=%u",
                 lock.locked() ? 1 : 0,
                 g_upload_file.isOpen() ? 1 : 0,
                 static_cast<unsigned long>(session_id),
                 static_cast<unsigned long>(g_upload_session_id),
                 static_cast<unsigned long long>(position),
                 static_cast<unsigned>(len));
        return false;
    }
    if (position != g_upload_position)
    {
        DBG_LOGW(TAG,
                 "write upload failed: non-sequential write path=%s pos=%llu current=%llu len=%u",
                 g_upload_path[0] ? g_upload_path : "(empty)",
                 static_cast<unsigned long long>(position),
                 static_cast<unsigned long long>(g_upload_position),
                 static_cast<unsigned>(len));
        return false;
    }
    if (len == 0)
    {
        return true;
    }
    if (!data)
    {
        DBG_LOGW(TAG, "write upload failed: null data path=%s len=%u", g_upload_path, static_cast<unsigned>(len));
        return false;
    }

    const size_t written = g_upload_file.write(data, len);
    note_file_activity();
    if (written != len)
    {
        DBG_LOGW(TAG,
                 "write upload failed: path=%s wanted=%u wrote=%u",
                 g_upload_path[0] ? g_upload_path : "(empty)",
                 static_cast<unsigned>(len),
                 static_cast<unsigned>(written));
        return false;
    }

    g_upload_position += static_cast<uint64_t>(written);
    if (g_upload_position - g_upload_last_sync_position >= kUploadSyncEveryBytes)
    {
        if (!g_upload_file.sync())
        {
            note_file_activity();
            DBG_LOGW(TAG,
                     "write upload failed: periodic sync failed path=%s size=%llu sd_error=0x%02X sd_data=0x%02X",
                     g_upload_path[0] ? g_upload_path : "(empty)",
                     static_cast<unsigned long long>(g_upload_position),
                     MicroSdCard::fs().sdErrorCode(),
                     MicroSdCard::fs().sdErrorData());
            return false;
        }
        note_file_activity();
        g_upload_last_sync_position = g_upload_position;
    }
    return true;
}

bool closeUploadFile(uint32_t session_id)
{
    note_file_activity();

    Lock lock;
    if (!lock.locked())
    {
        DBG_LOGW(TAG, "close upload failed: mutex unavailable");
        return false;
    }
    if (!g_upload_file.isOpen())
    {
        return true;
    }
    if (!upload_session_matches_locked(session_id))
    {
        DBG_LOGW(TAG,
                 "close upload failed: session mismatch session=%lu active=%lu",
                 static_cast<unsigned long>(session_id),
                 static_cast<unsigned long>(g_upload_session_id));
        return false;
    }

    bool ok = true;
    const uint64_t upload_file_size = g_upload_file.fileSize();
    note_file_activity();
    if (upload_file_size != g_upload_position && !g_upload_file.truncate(g_upload_position))
    {
        note_file_activity();
        ok = false;
        DBG_LOGW(TAG,
                 "close upload failed: truncate failed path=%s size=%llu file_size=%llu sd_error=0x%02X sd_data=0x%02X",
                 g_upload_path[0] ? g_upload_path : "(empty)",
                 static_cast<unsigned long long>(g_upload_position),
                 static_cast<unsigned long long>(upload_file_size),
                 MicroSdCard::fs().sdErrorCode(),
                 MicroSdCard::fs().sdErrorData());
    }
    const bool closed = g_upload_file.close();
    note_file_activity();
    if (!closed)
    {
        ok = false;
        DBG_LOGW(TAG,
                 "close upload failed: close failed path=%s size=%llu sd_error=0x%02X sd_data=0x%02X",
                 g_upload_path[0] ? g_upload_path : "(empty)",
                 static_cast<unsigned long long>(g_upload_position),
                 MicroSdCard::fs().sdErrorCode(),
                 MicroSdCard::fs().sdErrorData());
    }

    close_upload_locked();
    return ok;
}

void cancelUploadFile(uint32_t session_id)
{
    note_file_activity();

    Lock lock;
    if (lock.locked() && g_upload_file.isOpen() && upload_session_matches_locked(session_id))
    {
        close_upload_locked();
    }
}

uint64_t uploadFileSize(uint32_t session_id)
{
    Lock lock;
    return lock.locked() && upload_session_matches_locked(session_id) ? g_upload_position : 0;
}

bool writeFileChunk(const char* path, const uint8_t* data, size_t len, bool first_chunk)
{
    note_file_activity();

    Lock lock;
    if (!lock.locked() || !path || path[0] == '\0' || !MicroSdCard::isReady())
    {
        DBG_LOGW(TAG,
                 "write file failed: lock=%d path=%s sd_ready=%d len=%u first=%d",
                 lock.locked() ? 1 : 0,
                 path ? path : "(null)",
                 MicroSdCard::isReady() ? 1 : 0,
                 static_cast<unsigned>(len),
                 first_chunk ? 1 : 0);
        return false;
    }

    close_walk_locked();
    close_download_locked();
    if (g_upload_file.isOpen())
    {
        DBG_LOGW(TAG, "write file failed: upload active path=%s", g_upload_path[0] ? g_upload_path : "(empty)");
        return false;
    }

    FsFile        file;
    const oflag_t flags = O_WRONLY | O_CREAT | (first_chunk ? O_TRUNC : O_APPEND);
    if (!file.open(path, flags))
    {
        note_file_activity();
        DBG_LOGW(TAG, "write file failed: open failed path=%s first=%d", path, first_chunk ? 1 : 0);
        return false;
    }
    note_file_activity();

    const size_t written = len == 0 ? 0 : file.write(data, len);
    note_file_activity();
    const bool   ok      = written == len && file.sync();
    note_file_activity();
    if (!ok)
    {
        DBG_LOGW(TAG,
                 "write file failed: path=%s wanted=%u wrote=%u sync_or_write_failed",
                 path,
                 static_cast<unsigned>(len),
                 static_cast<unsigned>(written));
    }
    file.close();
    note_file_activity();
    return ok;
}

bool removeFile(const char* path)
{
    note_file_activity();

    Lock lock;
    if (!lock.locked() || !path || path[0] == '\0' || !MicroSdCard::isReady())
    {
        DBG_LOGW(TAG,
                 "remove file failed: lock=%d path=%s sd_ready=%d",
                 lock.locked() ? 1 : 0,
                 path ? path : "(null)",
                 MicroSdCard::isReady() ? 1 : 0);
        return false;
    }

    close_walk_locked();
    close_download_locked();
    if (g_upload_file.isOpen())
    {
        DBG_LOGW(TAG, "remove file failed: upload active path=%s", g_upload_path[0] ? g_upload_path : "(empty)");
        return false;
    }
    const bool removed = MicroSdCard::fs().remove(path);
    note_file_activity();
    if (!removed)
    {
        DBG_LOGW(TAG,
                 "remove file failed: fs remove failed path=%s sd_error=0x%02X sd_data=0x%02X",
                 path,
                 MicroSdCard::fs().sdErrorCode(),
                 MicroSdCard::fs().sdErrorData());
    }
    return removed;
}

bool isFile(const char* path)
{
    note_file_activity();

    Lock lock;
    if (!lock.locked() || !path || path[0] == '\0' || !MicroSdCard::isReady())
    {
        DBG_LOGW(TAG,
                 "is file failed: lock=%d path=%s sd_ready=%d",
                 lock.locked() ? 1 : 0,
                 path ? path : "(null)",
                 MicroSdCard::isReady() ? 1 : 0);
        return false;
    }
    if (g_upload_file.isOpen())
    {
        DBG_LOGW(TAG, "is file failed: upload active path=%s", g_upload_path[0] ? g_upload_path : "(empty)");
        return false;
    }

    FsFile file;
    if (!file.open(path, O_RDONLY))
    {
        note_file_activity();
        DBG_LOGW(TAG, "is file failed: open failed path=%s", path);
        return false;
    }
    note_file_activity();

    const bool result = file.isFile();
    file.close();
    note_file_activity();
    return result;
}

namespace
{

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

Lock::Lock()
{
    if (ensure_mutex())
    {
        m_locked = xSemaphoreTakeRecursive(g_mutex, portMAX_DELAY) == pdTRUE;
    }
}

Lock::~Lock()
{
    if (m_locked)
    {
        xSemaphoreGiveRecursive(g_mutex);
    }
}

bool Lock::locked() const
{
    return m_locked;
}

bool ensure_mutex()
{
    if (g_mutex)
    {
        return true;
    }

    g_mutex = xSemaphoreCreateRecursiveMutex();
    if (!g_mutex)
    {
        DBG_LOGE(TAG, "could not create filesystem mutex");
        return false;
    }
    return true;
}

void note_file_activity()
{
    g_last_file_activity_time = millis();
}

bool any_file_open_locked()
{
    return g_download_file.isOpen() || g_walk_root.isOpen() || g_walk_child.isOpen() || g_upload_file.isOpen();
}

void close_walk_locked()
{
    note_file_activity();
    g_walk_child.close();
    g_walk_root.close();
    note_file_activity();
}

void close_download_locked()
{
    note_file_activity();
    g_download_file.close();
    g_download_file_size = 0;
    g_download_position  = 0;
    g_download_path[0]   = '\0';
    note_file_activity();
}

void close_upload_locked()
{
    note_file_activity();
    g_upload_file.close();
    g_upload_position           = 0;
    g_upload_last_sync_position = 0;
    g_upload_path[0]            = '\0';
    g_upload_session_id         = 0;
    note_file_activity();
}

bool upload_session_matches_locked(uint32_t session_id)
{
    return session_id == 0 || session_id == g_upload_session_id;
}

bool reset_walk_locked()
{
    note_file_activity();
    close_download_locked();
    close_walk_locked();

    if (!MicroSdCard::isReady())
    {
        DBG_LOGW(TAG, "reset walk failed: microSD not ready");
        return false;
    }

    if (!g_walk_root.open("/", O_RDONLY))
    {
        note_file_activity();
        DBG_LOGW(TAG, "reset walk failed: could not open root directory");
        return false;
    }
    note_file_activity();
    return true;
}

bool reopen_download_locked()
{
    if (g_download_path[0] == '\0' || !MicroSdCard::isReady())
    {
        DBG_LOGW(TAG,
                 "reopen download failed: path=%s sd_ready=%d",
                 g_download_path[0] ? g_download_path : "(empty)",
                 MicroSdCard::isReady() ? 1 : 0);
        return false;
    }

    g_download_file.close();
    note_file_activity();
    if (!g_download_file.open(g_download_path, O_RDONLY) || g_download_file.isDir())
    {
        note_file_activity();
        DBG_LOGW(TAG, "reopen download failed: open failed path=%s", g_download_path);
        g_download_file.close();
        note_file_activity();
        return false;
    }
    note_file_activity();

    const uint64_t reopened_size = g_download_file.fileSize();
    note_file_activity();
    if (reopened_size != g_download_file_size)
    {
        DBG_LOGW(TAG,
                 "reopen download failed: size changed path=%s was=%llu now=%llu",
                 g_download_path,
                 static_cast<unsigned long long>(g_download_file_size),
                 static_cast<unsigned long long>(reopened_size));
        g_download_file.close();
        note_file_activity();
        return false;
    }

    if (g_download_position != 0 && !g_download_file.seekSet(g_download_position))
    {
        DBG_LOGW(TAG,
                 "reopen download failed: seek failed path=%s pos=%llu",
                 g_download_path,
                 static_cast<unsigned long long>(g_download_position));
        g_download_file.close();
        note_file_activity();
        return false;
    }

    return true;
}

int read_download_locked(uint64_t position, uint8_t* buffer, size_t max_len)
{
    if (position != g_download_position)
    {
        DBG_LOGW(TAG,
                 "read download failed: non-sequential read requested pos=%llu current=%llu size=%llu",
                 static_cast<unsigned long long>(position),
                 static_cast<unsigned long long>(g_download_position),
                 static_cast<unsigned long long>(g_download_file_size));
        return -1;
    }

    const int bytes_read = g_download_file.read(buffer, max_len);
    note_file_activity();
    if (bytes_read > 0)
    {
        g_download_position += static_cast<uint64_t>(bytes_read);
    }
    return bytes_read;
}

} // namespace

} // namespace AsyncFsManager
