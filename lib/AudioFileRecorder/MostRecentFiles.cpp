#include "MostRecentFiles.h"

#include <string.h>

#include "SdCard.h"

namespace MostRecentFiles
{
namespace
{

struct Candidate
{
    uint32_t created = 0;
    char     name[kMaxFileNameLength] = {};
};

uint32_t created_key(uint16_t date, uint16_t time)
{
    return (static_cast<uint32_t>(date) << 16) | time;
}

bool is_recorded_file(FsFile& file)
{
    const int attributes = file.attrib();
    return file.isFile() && attributes >= 0 && (attributes & FS_ATTRIB_ARCHIVE);
}

bool newer_than(const Candidate& lhs, uint32_t rhsCreated, const char* rhsName)
{
    if (rhsCreated != lhs.created)
    {
        return rhsCreated > lhs.created;
    }
    return strcmp(rhsName, lhs.name) > 0;
}

void insert_candidate(Candidate (&recent)[kMaxFiles], size_t& count, const char* name, uint32_t created)
{
    size_t insert_at = 0;
    while (insert_at < count && !newer_than(recent[insert_at], created, name))
    {
        ++insert_at;
    }

    if (insert_at >= kMaxFiles)
    {
        return;
    }

    const size_t move_count = count < kMaxFiles ? count : kMaxFiles - 1;
    for (size_t i = move_count; i > insert_at; --i)
    {
        recent[i] = recent[i - 1];
    }

    recent[insert_at].created = created;
    strlcpy(recent[insert_at].name, name, sizeof(recent[insert_at].name));

    if (count < kMaxFiles)
    {
        ++count;
    }
}

} // namespace

FileList get()
{
    FileList result;

    FsFile root;
    if (!root.open("/", O_RDONLY))
    {
        return result;
    }

    Candidate recent[kMaxFiles];
    FsFile    file;
    char      name[kMaxFileNameLength];
    uint16_t  date = 0;
    uint16_t  time = 0;

    while (file.openNext(&root, O_RDONLY))
    {
        if (is_recorded_file(file) && file.getCreateDateTime(&date, &time))
        {
            file.getName(name, sizeof(name));
            if (name[0] != '\0')
            {
                insert_candidate(recent, result.count, name, created_key(date, time));
            }
        }
        file.close();
    }

    root.close();

    for (size_t i = 0; i < result.count; ++i)
    {
        strlcpy(result.names[i], recent[i].name, sizeof(result.names[i]));
    }

    return result;
}

} // namespace MostRecentFiles
