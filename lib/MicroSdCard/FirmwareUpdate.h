#pragma once

#include "thefly_common.h"
#include <stdint.h>

namespace MicroSdCard
{

enum class FirmwareUpdateResult : uint8_t
{
    Ok,
    CardNotReady,
    FileOpenFailed,
    EmptyFile,
    NoUpdatePartition,
    FileTooLarge,
    OtaBeginFailed,
    FileReadFailed,
    OtaWriteFailed,
    OtaEndFailed,
    SetBootPartitionFailed,
};

using FirmwareUpdateProgressCallback = void (*)(uint64_t bytes_written, uint64_t bytes_total);

FirmwareUpdateResult update_firmware(FirmwareUpdateProgressCallback progressCallback = nullptr);

} // namespace MicroSdCard
