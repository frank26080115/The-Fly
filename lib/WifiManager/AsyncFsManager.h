#pragma once

#include <stddef.h>
#include <stdint.h>

#include <Arduino.h>

namespace AsyncFsManager
{

enum class WalkResult
{
    File,
    End,
    NotReady,
    Error,
};

bool init();
bool isReady();
bool guiShouldYield();

bool       resetWalk();
WalkResult walkOne(char* file_name, size_t file_name_size);
void       closeWalk();

bool     openFileForDownload(const char* path, uint64_t* file_size = nullptr);
int      readFileChunk(uint64_t position, uint8_t* buffer, size_t max_len);
uint64_t openFileSize();
void     closeFile();

bool     openFileForUpload(const char* path, uint64_t expected_size = 0, uint32_t* session_id = nullptr);
bool     writeUploadFileChunk(uint32_t session_id, uint64_t position, const uint8_t* data, size_t len);
bool     closeUploadFile(uint32_t session_id = 0);
void     cancelUploadFile(uint32_t session_id = 0);
uint64_t uploadFileSize(uint32_t session_id = 0);

bool writeFileChunk(const char* path, const uint8_t* data, size_t len, bool first_chunk);
bool removeFile(const char* path);
bool isFile(const char* path);

} // namespace AsyncFsManager
