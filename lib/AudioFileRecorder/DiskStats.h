#pragma once

#include <cstdint>

namespace DiskStats
{

bool refreshDiskSpace();
bool refreshRecordingUploadStats();
void drawDiskSpaceWarning();

bool     diskSpace(uint64_t& total_bytes, uint64_t& free_bytes);
uint64_t totalDiskSpace();
uint64_t freeDiskSpace();
uint8_t  freeDiskSpacePercent();

uint32_t totalRecFilesStored();
uint32_t totalRecFilesNotUploaded();

const char* lastUploadDateTime();
const char* latestRecordedFileName();

} // namespace DiskStats
