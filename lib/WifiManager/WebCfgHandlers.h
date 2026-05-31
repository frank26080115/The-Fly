#pragma once

#include <Arduino.h>

class AsyncWebServerRequest;

namespace WebCfgHandlers
{
void sendCfg(AsyncWebServerRequest* request);
void finishSetCfg(AsyncWebServerRequest* request);
#if BUILD_WITH_SECURITY_LEVEL >= 1
void writePinCodeBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
void finishResetPinCode(AsyncWebServerRequest* request);
#if BUILD_WITH_SECURITY_LEVEL == 1
void finishSetCustomPin(AsyncWebServerRequest* request);
#endif
#endif
void timeSync(AsyncWebServerRequest* request);
void writeSetCfgBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
}
