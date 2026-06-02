/*
The purpose of this module is to list the few most recently recorded files
so that we don't overwhelm the user with too many files in the GUI
*/

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "MostRecentFiles.h"

#include <new>
#include <string.h>

#include "MicroSdCard.h"
#include "utilfuncs.h"

namespace MostRecentFiles
{
namespace
{

// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------

struct Candidate
{
    uint32_t created                  = 0;
    char     name[kMaxFileNameLength] = {};
};

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

void     insert_candidate(Candidate* recent, size_t capacity, size_t& count, const char* name, uint32_t created);
bool     newer_than(const Candidate& lhs, uint32_t rhsCreated, const char* rhsName);
bool     is_listable_recording_name(const char* name);
bool     is_recorded_file(FsFile& file);
uint32_t created_key(uint16_t date, uint16_t time);

} // namespace

// -----------------------------------------------------------------------------
// File List
// -----------------------------------------------------------------------------

FileList::FileList(size_t requestedCapacity)
{
    init(requestedCapacity);
}

FileList::FileList(const FileList& other)
{
    if (!init(other.capacity))
    {
        return;
    }

    count = other.count;
    if (names && other.names && count > 0)
    {
        memcpy(names, other.names, count * kMaxFileNameLength);
    }
}

FileList& FileList::operator=(const FileList& other)
{
    if (this == &other)
    {
        return *this;
    }

    char (*new_names)[kMaxFileNameLength] = nullptr;
    if (other.capacity > 0)
    {
        new_names = new (std::nothrow) char[other.capacity][kMaxFileNameLength];
        if (!new_names)
        {
            return *this;
        }
        memset(new_names, 0, other.capacity * kMaxFileNameLength);
        if (other.names && other.count > 0)
        {
            memcpy(new_names, other.names, other.count * kMaxFileNameLength);
        }
    }

    delete[] names;
    names    = new_names;
    count    = other.count;
    capacity = other.capacity;
    return *this;
}

FileList::FileList(FileList&& other) noexcept
{
    names          = other.names;
    count          = other.count;
    capacity       = other.capacity;
    other.names    = nullptr;
    other.count    = 0;
    other.capacity = 0;
}

FileList& FileList::operator=(FileList&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    delete[] names;
    names          = other.names;
    count          = other.count;
    capacity       = other.capacity;
    other.names    = nullptr;
    other.count    = 0;
    other.capacity = 0;
    return *this;
}

FileList::~FileList()
{
    reset();
}

bool FileList::init(size_t requestedCapacity)
{
    reset();
    if (requestedCapacity == 0)
    {
        return true;
    }

    names = new (std::nothrow) char[requestedCapacity][kMaxFileNameLength];
    if (!names)
    {
        return false;
    }

    memset(names, 0, requestedCapacity * kMaxFileNameLength);
    capacity = requestedCapacity;
    return true;
}

void FileList::reset()
{
    delete[] names;
    names    = nullptr;
    count    = 0;
    capacity = 0;
}

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

FileList get(size_t maxFiles)
{
    FileList result;
    if (!result.init(maxFiles) || maxFiles == 0)
    {
        return result;
    }

    FsFile root;
    if (!root.open("/", O_RDONLY))
    {
        return result;
    }

    Candidate* recent = new (std::nothrow) Candidate[maxFiles];
    if (!recent)
    {
        root.close();
        return result;
    }

    FsFile   file;
    char     name[kMaxFileNameLength] = {};
    uint16_t date                     = 0;
    uint16_t time                     = 0;

    while (file.openNext(&root, O_RDONLY))
    {
        if (is_recorded_file(file) && file.getCreateDateTime(&date, &time))
        {
            name[0] = '\0';
            file.getName(name, sizeof(name));
            if (is_listable_recording_name(name))
            {
                insert_candidate(recent, maxFiles, result.count, name, created_key(date, time));
            }
        }
        file.close();
        taskYIELD();
    }

    root.close();

    for (size_t i = 0; i < result.count; ++i)
    {
        strlcpy(result.names[i], recent[i].name, sizeof(result.names[i]));
    }

    delete[] recent;
    return result;
}

namespace
{

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

void insert_candidate(Candidate* recent, size_t capacity, size_t& count, const char* name, uint32_t created)
{
    if (!recent || capacity == 0)
    {
        return;
    }

    size_t insert_at = 0;
    while (insert_at < count && !newer_than(recent[insert_at], created, name))
    {
        ++insert_at;
    }

    if (insert_at >= capacity)
    {
        return;
    }

    const size_t move_count = count < capacity ? count : capacity - 1;
    for (size_t i = move_count; i > insert_at; --i)
    {
        recent[i] = recent[i - 1];
    }

    recent[insert_at].created = created;
    strlcpy(recent[insert_at].name, name, sizeof(recent[insert_at].name));

    if (count < capacity)
    {
        ++count;
    }
}

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

bool newer_than(const Candidate& lhs, uint32_t rhsCreated, const char* rhsName)
{
    if (rhsCreated != lhs.created)
    {
        return rhsCreated > lhs.created;
    }
    return strcmp(rhsName, lhs.name) > 0;
}

bool is_listable_recording_name(const char* name)
{
    if (ends_with_case_insensitive(name, ".wav") || ends_with_case_insensitive(name, ".mp3"))
    {
        return true;
    }

#ifdef BUILD_WITH_ENCRYPTED_PLAYBACK
    return ends_with_case_insensitive(name, ".rec") || ends_with_case_insensitive(name, ".fly");
#else
    return false;
#endif
}

bool is_recorded_file(FsFile& file)
{
    const int attributes = file.attrib();
    return file.isFile() && attributes >= 0 && (attributes & FS_ATTRIB_ARCHIVE);
}

uint32_t created_key(uint16_t date, uint16_t time)
{
    return (static_cast<uint32_t>(date) << 16) | time;
}

} // namespace

} // namespace MostRecentFiles
