#pragma once

#include <Arduino.h>

#include <stddef.h>
#include <stdint.h>

class AsyncWebParameter;
class AsyncWebServerRequest;

class WebServer
{
public:
    enum class RequestAuthResult
    {
        Ok,
        MissingTimestamp,
        BadTimestamp,
        ClockNotReady,
        TimestampOutsideWindow,
        MissingHash,
        BadHash,
        MasterKeyUnavailable,
        HashFailed,
        HashMismatch,
    };

    static bool init();

    static RequestAuthResult authenticateRequest(AsyncWebServerRequest* request);
    static bool              requestIsAuthenticated(AsyncWebServerRequest* request);
    static const char*       requestAuthResultName(RequestAuthResult result);
    static const AsyncWebParameter* findRequestParam(AsyncWebServerRequest* request, const char* name);
    static String            jsonString(const char* text);
    static void              formatMac(const uint8_t* bdaddr, char* buffer, size_t buffer_size);
};
