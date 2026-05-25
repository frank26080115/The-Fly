#pragma once

#include <stddef.h>
#include <stdint.h>

#include <Arduino.h>

#include "Aegis.h"

class AsyncWebParameter;
class AsyncWebServerRequest;

class WebServer
{
public:
    static constexpr size_t kSessionChallengeSize = 32;
    static constexpr size_t kSessionResponseSize  = Aegis::kSha256Size;
    static constexpr size_t kSessionSaltHalfSize  = 16;
    static constexpr size_t kSessionSaltSize      = kSessionSaltHalfSize * 2;
    static constexpr size_t kSessionKeySize       = Aegis::kNetworkKeySize;
    static constexpr size_t kSessionGcmNonceSize  = 12;

    struct SessionSecurityState
    {
        uint8_t  session_challenge[kSessionChallengeSize] = {};
        uint8_t  session_response_from_client[kSessionResponseSize] = {};
        uint8_t  session_response_from_server[kSessionResponseSize] = {};
        uint8_t  session_salt_from_server[kSessionSaltHalfSize] = {};
        uint8_t  session_salt_from_client[kSessionSaltHalfSize] = {};
        uint8_t  session_key[kSessionKeySize] = {};
        uint64_t nonce_counter = 0;
        bool     challenge_valid = false;
        bool     response_valid = false;
        bool     session_key_valid = false;
    };

    enum class SessionAuthResult
    {
        Ok,
        SessionUnavailable,
        MissingClientResponse,
        BadClientResponse,
        MissingClientSalt,
        BadClientSalt,
        NetworkKeyUnavailable,
        SessionKeyFailed,
    };

    static bool init();

    static SessionAuthResult authenticateSessionRequest(AsyncWebServerRequest* request, uint8_t out_session_key[kSessionKeySize]);
    static const char*       sessionAuthResultName(SessionAuthResult result);
    static uint64_t          nextSessionNonceCounter();
    static void              fillSessionNonce(uint64_t counter, uint8_t nonce[kSessionGcmNonceSize]);
    static const AsyncWebParameter* findRequestParam(AsyncWebServerRequest* request, const char* name);
    static String            jsonString(const char* text);
    static void              formatMac(const uint8_t* bdaddr, char* buffer, size_t buffer_size);
};
