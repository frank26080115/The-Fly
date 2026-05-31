#pragma once

#include "thefly_common.h"

#ifdef BUILD_WITH_DECRYPTED_DOWNLOAD

#include <Arduino.h>

class AsyncWebServerRequest;

namespace DecryptedDownload
{
void finish(AsyncWebServerRequest* request);
void writeBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
}

#endif
