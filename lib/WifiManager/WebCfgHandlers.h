#pragma once

#include <Arduino.h>

class AsyncWebServerRequest;

namespace WebCfgHandlers
{
void sendCfg(AsyncWebServerRequest* request);
void finishSetCfg(AsyncWebServerRequest* request);
void timeSync(AsyncWebServerRequest* request);
void writeSetCfgBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
}
