#pragma once

#include "thefly_common.h"
#include <stddef.h>

namespace CallManager
{

enum class PhoneUiState
{
    Idle,
    IncomingCall,
    OutgoingCall,
    ActiveCall,
    HeldCall,
    AudioConnected,
};

struct PhoneState
{
    int  call      = 0;
    int  callsetup = 0;
    int  callheld  = 0;
    bool scoAudio  = false;
};

void reset();

void onBluetoothConnectionEstablished(const char* friendlyName);
void onBluetoothDisconnected();
void setCallStatus(int status);
void setCallSetupStatus(int status);
void setCallHeldStatus(int status);
void setScoAudioConnected(bool connected);

PhoneUiState uiState();
PhoneState   phoneState();
const char*  uiStateName(PhoneUiState state);

bool formatCallMetaText(char* out, size_t outSize);

bool        addCallerInfo(const char* text);
void        clearCallerInfo();
size_t      getCallerInfoCnt();
const char* getCallerInfoAt(size_t index);

} // namespace CallManager
