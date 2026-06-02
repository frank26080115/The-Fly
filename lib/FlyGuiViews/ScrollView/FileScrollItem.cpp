#include "FileScrollItem.h"

#include "sprites.h"

namespace
{

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

constexpr uint32_t kFnvOffsetBasis = 2166136261u;
constexpr uint32_t kFnvPrime       = 16777619u;
constexpr uint32_t kOverlayOffset  = 25u;

// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------

struct SpriteChoice
{
    const uint8_t* data     = nullptr;
    uint32_t       width    = 0;
    uint32_t       height   = 0;
    size_t         byte_cnt = 0;
};

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

const SpriteChoice kFilebackBlue = {
    sprite_fileback_blue,
    SPRITE_FILEBACK_BLUE_WIDTH,
    SPRITE_FILEBACK_BLUE_HEIGHT,
    SPRITE_FILEBACK_BLUE_BYTES,
};

const SpriteChoice kFilebackYellow = {
    sprite_fileback_yellow,
    SPRITE_FILEBACK_YELLOW_WIDTH,
    SPRITE_FILEBACK_YELLOW_HEIGHT,
    SPRITE_FILEBACK_YELLOW_BYTES,
};

const SpriteChoice kFiletopAlarm = {
    sprite_filetop_alarm,
    SPRITE_FILETOP_ALARM_WIDTH,
    SPRITE_FILETOP_ALARM_HEIGHT,
    SPRITE_FILETOP_ALARM_BYTES,
};

const SpriteChoice kFiletopCalendar = {
    sprite_filetop_calendar,
    SPRITE_FILETOP_CALENDAR_WIDTH,
    SPRITE_FILETOP_CALENDAR_HEIGHT,
    SPRITE_FILETOP_CALENDAR_BYTES,
};

const SpriteChoice kFiletopIdea = {
    sprite_filetop_idea,
    SPRITE_FILETOP_IDEA_WIDTH,
    SPRITE_FILETOP_IDEA_HEIGHT,
    SPRITE_FILETOP_IDEA_BYTES,
};

const SpriteChoice kFiletopJournal = {
    sprite_filetop_journal,
    SPRITE_FILETOP_JOURNAL_WIDTH,
    SPRITE_FILETOP_JOURNAL_HEIGHT,
    SPRITE_FILETOP_JOURNAL_BYTES,
};

const SpriteChoice kFiletopMic = {
    sprite_filetop_mic,
    SPRITE_FILETOP_MIC_WIDTH,
    SPRITE_FILETOP_MIC_HEIGHT,
    SPRITE_FILETOP_MIC_BYTES,
};

const SpriteChoice kFiletopRecord = {
    sprite_filetop_record,
    SPRITE_FILETOP_RECORD_WIDTH,
    SPRITE_FILETOP_RECORD_HEIGHT,
    SPRITE_FILETOP_RECORD_BYTES,
};

const SpriteChoice kFiletopSpeaker = {
    sprite_filetop_speaker,
    SPRITE_FILETOP_SPEAKER_WIDTH,
    SPRITE_FILETOP_SPEAKER_HEIGHT,
    SPRITE_FILETOP_SPEAKER_BYTES,
};

const SpriteChoice kFiletopTodo = {
    sprite_filetop_todo,
    SPRITE_FILETOP_TODO_WIDTH,
    SPRITE_FILETOP_TODO_HEIGHT,
    SPRITE_FILETOP_TODO_BYTES,
};

const SpriteChoice kFiletopTodolist = {
    sprite_filetop_todolist,
    SPRITE_FILETOP_TODOLIST_WIDTH,
    SPRITE_FILETOP_TODOLIST_HEIGHT,
    SPRITE_FILETOP_TODOLIST_BYTES,
};

const SpriteChoice kFiletopWaveform = {
    sprite_filetop_waveform,
    SPRITE_FILETOP_WAVEFORM_WIDTH,
    SPRITE_FILETOP_WAVEFORM_HEIGHT,
    SPRITE_FILETOP_WAVEFORM_BYTES,
};

const SpriteChoice kGenericTops[] = {
    kFiletopMic,
    kFiletopRecord,
    kFiletopSpeaker,
    kFiletopWaveform,
};

const SpriteChoice kYellowSafeGenericTops[] = {
    kFiletopMic,
    kFiletopSpeaker,
    kFiletopWaveform,
};

const SpriteChoice kReminderTops[] = {
    kFiletopAlarm,
    kFiletopCalendar,
};

const SpriteChoice kTodoTops[] = {
    kFiletopTodo,
    kFiletopTodolist,
};

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static uint32_t      hash_file_name(const char* fileName);
static sprite_desc_t make_file_sprite(const char* fileName, uint32_t hash);
static SpriteChoice  file_backdrop_for(uint32_t hash);
static SpriteChoice  file_top_for(const char* fileName, uint32_t hash, const SpriteChoice& backdrop);
static SpriteChoice  generic_top_for(uint32_t hash, const SpriteChoice& backdrop);
static char          starting_type_code(const char* fileName);
static char          upper_ascii(char value);
static bool          is_yellow_backdrop(const SpriteChoice& sprite);
static bool          is_record_top(const SpriteChoice& sprite);

} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

FileScrollItem::FileScrollItem() = default;

void FileScrollItem::configureFile(ScrollItemKind kind, int32_t callbackValue, const char* fileName)
{
    fileHash_ = hash_file_name(fileName);
    configureSprite(kind, callbackValue, fileName, make_file_sprite(fileName, fileHash_));
}

namespace
{

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

uint32_t hash_file_name(const char* fileName)
{
    uint32_t hash = kFnvOffsetBasis;
    for (const char* cursor = fileName ? fileName : ""; *cursor; ++cursor)
    {
        hash ^= static_cast<uint8_t>(*cursor);
        hash *= kFnvPrime;
    }
    return hash;
}

sprite_desc_t make_file_sprite(const char* fileName, uint32_t hash)
{
    const SpriteChoice backdrop = file_backdrop_for(hash);
    const SpriteChoice top      = file_top_for(fileName, hash, backdrop);

    return {
        backdrop.data,
        backdrop.width,
        backdrop.height,
        backdrop.byte_cnt,
        top.data,
        top.width,
        top.height,
        top.byte_cnt,
        kOverlayOffset,
        kOverlayOffset,
    };
}

SpriteChoice file_backdrop_for(uint32_t hash)
{
    return (hash & 1u) ? kFilebackYellow : kFilebackBlue;
}

SpriteChoice file_top_for(const char* fileName, uint32_t hash, const SpriteChoice& backdrop)
{
    switch (starting_type_code(fileName))
    {
    case 'R':
        return kReminderTops[(hash >> 1) % (sizeof(kReminderTops) / sizeof(kReminderTops[0]))];
    case 'I':
        return kFiletopIdea;
    case 'J':
        return kFiletopJournal;
    case 'T':
        return kTodoTops[(hash >> 1) % (sizeof(kTodoTops) / sizeof(kTodoTops[0]))];
    case 'C':
    case 'M':
    case 'U':
    default:
        return generic_top_for(hash, backdrop);
    }
}

SpriteChoice generic_top_for(uint32_t hash, const SpriteChoice& backdrop)
{
    const SpriteChoice top = kGenericTops[(hash >> 1) % (sizeof(kGenericTops) / sizeof(kGenericTops[0]))];
    if (!is_yellow_backdrop(backdrop) || !is_record_top(top))
    {
        return top;
    }

    return kYellowSafeGenericTops[(hash >> 3) % (sizeof(kYellowSafeGenericTops) / sizeof(kYellowSafeGenericTops[0]))];
}

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

char starting_type_code(const char* fileName)
{
    const char* name = fileName ? fileName : "";
    for (const char* cursor = name; *cursor; ++cursor)
    {
        if (*cursor == '/' || *cursor == '\\')
        {
            name = cursor + 1;
        }
    }

    return upper_ascii(name[0]);
}

char upper_ascii(char value)
{
    if (value >= 'a' && value <= 'z')
    {
        return static_cast<char>(value - 'a' + 'A');
    }

    return value;
}

bool is_yellow_backdrop(const SpriteChoice& sprite)
{
    return sprite.data == kFilebackYellow.data;
}

bool is_record_top(const SpriteChoice& sprite)
{
    return sprite.data == kFiletopRecord.data;
}

} // namespace
