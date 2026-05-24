#pragma once

#include <Arduino.h>

class AsyncWebServerRequest;

namespace WebFileHandlers
{
void sendMicroSdFile(AsyncWebServerRequest* request);
void deleteMicroSdFile(AsyncWebServerRequest* request);
void sendMicroSdFileList(AsyncWebServerRequest* request);
void finishFileUpload(AsyncWebServerRequest* request);
void writeFileUploadPart(AsyncWebServerRequest* request,
                         const String& filename,
                         size_t index,
                         uint8_t* data,
                         size_t len,
                         bool final);
void writeFileUploadBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
}
