#pragma once

#include "thefly_common.h"
#include <cstddef>
#include <cstdint>

namespace MostRecentFiles
{

constexpr size_t kDefaultMaxFiles   = MOST_RECENT_FILES_MAX_FILES;
constexpr size_t kMaxFiles          = kDefaultMaxFiles;
constexpr size_t kMaxFileNameLength = 48;

struct FileList
{
    FileList() = default;
    explicit FileList(size_t capacity);
    FileList(const FileList& other);
    FileList& operator=(const FileList& other);
    FileList(FileList&& other) noexcept;
    FileList& operator=(FileList&& other) noexcept;
    ~FileList();

    bool init(size_t capacity);
    void reset();

    const char* operator[](size_t index) const
    {
        return names && index < count ? names[index] : "";
    }

    uint64_t fileSize(size_t index) const
    {
        return file_sizes && index < count ? file_sizes[index] : 0;
    }

    size_t count                      = 0;
    size_t capacity                   = 0;
    char (*names)[kMaxFileNameLength] = nullptr;
    uint64_t* file_sizes              = nullptr;
};

FileList get(size_t maxFiles = kDefaultMaxFiles);

} // namespace MostRecentFiles
