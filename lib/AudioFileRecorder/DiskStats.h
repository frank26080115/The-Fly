#pragma once

#include <cstdint>

namespace DiskStats
{

bool refreshDiskSpace();
bool refreshRecordingUploadStats();

uint64_t totalDiskSpace();
uint64_t freeDiskSpace();
uint8_t  freeDiskSpacePercent();

uint32_t totalRecFilesStored();
uint32_t totalRecFilesNotUploaded();

const char* lastUploadDateTime();
const char* latestRecordedFileName();

} // namespace DiskStats
