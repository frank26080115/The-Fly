#pragma once

#include <cstddef>

namespace MostRecentFiles
{

constexpr size_t kMaxFiles          = 6;
constexpr size_t kMaxFileNameLength = 48;

struct FileList
{
    size_t count = 0;
    char   names[kMaxFiles][kMaxFileNameLength] = {};

    const char* operator[](size_t index) const
    {
        return index < count ? names[index] : "";
    }
};

FileList get();

} // namespace MostRecentFiles
