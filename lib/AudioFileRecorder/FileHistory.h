#pragma once

#include "thefly_common.h"
#include <cstdint>

namespace AudioFileRecorder
{

enum class PruneHistoryState
{
    Idle,
    Busy,
    Done,
    Error,
};

enum class PruneHistoryError
{
    None,
    AlreadyBusy,
    SdNotReady,
    FileRotateFailed,
    FileOpenFailed,
    FileWriteFailed,
    ClockReadFailed,
    TaskCreateFailed,
};

struct PruneHistoryStatus
{
    PruneHistoryState state           = PruneHistoryState::Idle;
    PruneHistoryError error           = PruneHistoryError::None;
    uint32_t          lines_processed = 0;
    uint32_t          lines_kept      = 0;
    uint32_t          files_pruned    = 0;
    uint32_t          messages_pruned = 0;
    uint32_t          delete_failures = 0;
    bool              finished        = false;
};

using PruneHistoryCallback = void (*)(const PruneHistoryStatus& status);

bool               prune_history();
PruneHistoryStatus prune_history_status();
void               set_prune_history_complete_callback(PruneHistoryCallback callback);

} // namespace AudioFileRecorder
