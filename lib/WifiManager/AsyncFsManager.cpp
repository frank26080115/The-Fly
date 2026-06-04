// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "AsyncFsManager.h"

#include <SdFat.h>
#include <atomic>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "MicroSdCard.h"
#include "dbg_log.h"

namespace AsyncFsManager
{
namespace
{

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

constexpr const char* TAG               = "AsyncFsManager";
constexpr size_t      kDownloadPathSize = 256;

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
uint64_t          g_download_file_size               = 0;
uint64_t          g_download_position                = 0;
char              g_download_path[kDownloadPathSize] = {};
std::atomic<bool> g_web_upload_active                = {false};
std::atomic<bool> g_gui_update_active                = {false};
std::atomic<TaskHandle_t> g_gui_update_task          = {nullptr};

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

bool ensure_mutex();
void close_walk_locked();
void close_download_locked();
bool reset_walk_locked();
bool reopen_download_locked();
int  read_download_locked(uint64_t position, uint8_t* buffer, size_t max_len);
bool current_task_owns_gui_update();
void wait_for_gui_update_idle();

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
    return true;
}

bool isReady()
{
    Lock lock;
    return lock.locked() && MicroSdCard::isReady();
}

bool guiShouldYield()
{
    if (g_web_upload_active.load(std::memory_order_relaxed) ||
        g_gui_update_active.load(std::memory_order_acquire))
    {
        return true;
    }

    Lock lock;
    return lock.locked() && (g_download_file.isOpen() || g_walk_root.isOpen());
}

void beginGuiUpdate()
{
    Lock lock;
    if (!lock.locked())
    {
        return;
    }

    g_gui_update_task.store(xTaskGetCurrentTaskHandle(), std::memory_order_release);
    g_gui_update_active.store(true, std::memory_order_release);
}

void endGuiUpdate()
{
    g_gui_update_active.store(false, std::memory_order_release);
    g_gui_update_task.store(nullptr, std::memory_order_release);
}

void beginWebUpload()
{
    g_web_upload_active.store(true, std::memory_order_relaxed);
}

void endWebUpload()
{
    g_web_upload_active.store(false, std::memory_order_relaxed);
}

bool resetWalk()
{
    Lock lock;
    return lock.locked() && reset_walk_locked();
}

WalkResult walkOne(char* file_name, size_t file_name_size)
{
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
    Lock lock;
    if (lock.locked())
    {
        close_walk_locked();
    }
}

bool openFileForDownload(const char* path, uint64_t* file_size)
{
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
        DBG_LOGW(TAG, "open download failed: could not open path=%s", path);
        close_download_locked();
        return false;
    }
    if (g_download_file.isDir())
    {
        DBG_LOGW(TAG, "open download failed: path is directory path=%s", path);
        close_download_locked();
        return false;
    }
    if (!g_download_file.seekSet(0))
    {
        DBG_LOGW(TAG, "open download failed: seek to start failed path=%s", path);
        close_download_locked();
        return false;
    }

    g_download_file_size = g_download_file.fileSize();
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
    Lock lock;
    if (lock.locked())
    {
        close_download_locked();
    }
}

bool writeFileChunk(const char* path, const uint8_t* data, size_t len, bool first_chunk)
{
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

    FsFile        file;
    const oflag_t flags = O_WRONLY | O_CREAT | (first_chunk ? O_TRUNC : O_APPEND);
    if (!file.open(path, flags))
    {
        DBG_LOGW(TAG,
                 "write file failed: open failed path=%s first=%d sd_error=0x%02X sd_data=0x%02X",
                 path,
                 first_chunk ? 1 : 0,
                 MicroSdCard::fs().sdErrorCode(),
                 MicroSdCard::fs().sdErrorData());
        return false;
    }

    const size_t written = len == 0 ? 0 : file.write(data, len);
    const bool   ok      = written == len && file.sync();
    if (!ok)
    {
        DBG_LOGW(TAG,
                 "write file failed: path=%s wanted=%u wrote=%u sync_or_write_failed sd_error=0x%02X sd_data=0x%02X",
                 path,
                 static_cast<unsigned>(len),
                 static_cast<unsigned>(written),
                 MicroSdCard::fs().sdErrorCode(),
                 MicroSdCard::fs().sdErrorData());
    }
    file.close();
    return ok;
}

bool removeFile(const char* path)
{
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
    const bool removed = MicroSdCard::fs().remove(path);
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

    FsFile file;
    if (!file.open(path, O_RDONLY))
    {
        DBG_LOGW(TAG, "is file failed: open failed path=%s", path);
        return false;
    }

    const bool result = file.isFile();
    file.close();
    return result;
}

namespace
{

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

Lock::Lock()
{
    wait_for_gui_update_idle();
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

void close_walk_locked()
{
    g_walk_child.close();
    g_walk_root.close();
}

void close_download_locked()
{
    g_download_file.close();
    g_download_file_size = 0;
    g_download_position  = 0;
    g_download_path[0]   = '\0';
}

bool reset_walk_locked()
{
    close_download_locked();
    close_walk_locked();

    if (!MicroSdCard::isReady())
    {
        DBG_LOGW(TAG, "reset walk failed: microSD not ready");
        return false;
    }

    if (!g_walk_root.open("/", O_RDONLY))
    {
        DBG_LOGW(TAG, "reset walk failed: could not open root directory");
        return false;
    }
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
    if (!g_download_file.open(g_download_path, O_RDONLY) || g_download_file.isDir())
    {
        DBG_LOGW(TAG, "reopen download failed: open failed path=%s", g_download_path);
        g_download_file.close();
        return false;
    }

    const uint64_t reopened_size = g_download_file.fileSize();
    if (reopened_size != g_download_file_size)
    {
        DBG_LOGW(TAG,
                 "reopen download failed: size changed path=%s was=%llu now=%llu",
                 g_download_path,
                 static_cast<unsigned long long>(g_download_file_size),
                 static_cast<unsigned long long>(reopened_size));
        g_download_file.close();
        return false;
    }

    if (g_download_position != 0 && !g_download_file.seekSet(g_download_position))
    {
        DBG_LOGW(TAG,
                 "reopen download failed: seek failed path=%s pos=%llu",
                 g_download_path,
                 static_cast<unsigned long long>(g_download_position));
        g_download_file.close();
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
    if (bytes_read > 0)
    {
        g_download_position += static_cast<uint64_t>(bytes_read);
    }
    return bytes_read;
}

bool current_task_owns_gui_update()
{
    return g_gui_update_active.load(std::memory_order_acquire) &&
           g_gui_update_task.load(std::memory_order_acquire) == xTaskGetCurrentTaskHandle();
}

void wait_for_gui_update_idle()
{
    while (g_gui_update_active.load(std::memory_order_acquire) && !current_task_owns_gui_update())
    {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

} // namespace

} // namespace AsyncFsManager
