#include "CallManager.h"

#include <Arduino.h>
#include <mutex>
#include <stdlib.h>
#include <string.h>

#include "utilfuncs.h"

namespace CallManager
{
namespace
{

struct CallerInfoItem
{
    char*           text = nullptr;
    CallerInfoItem* next = nullptr;
};

std::mutex      g_mutex;
PhoneState      g_phone_state = {};
CallerInfoItem* g_info_head   = nullptr;
CallerInfoItem* g_info_tail   = nullptr;
size_t          g_info_count  = 0;

PhoneUiState ui_state_from_phone_state(const PhoneState& state)
{
    if (state.callsetup == 1)
    {
        return PhoneUiState::IncomingCall;
    }

    if (state.callsetup == 2 || state.callsetup == 3)
    {
        return PhoneUiState::OutgoingCall;
    }

    if (state.callheld != 0)
    {
        return PhoneUiState::HeldCall;
    }

    if (state.call == 1)
    {
        return PhoneUiState::ActiveCall;
    }

    if (state.scoAudio)
    {
        return PhoneUiState::AudioConnected;
    }

    return PhoneUiState::Idle;
}

bool has_info_unlocked(const char* text)
{
    for (CallerInfoItem* item = g_info_head; item; item = item->next)
    {
        if (item->text && strcmp(item->text, text) == 0)
        {
            return true;
        }
    }

    return false;
}

void clear_info_unlocked()
{
    CallerInfoItem* item = g_info_head;
    while (item)
    {
        CallerInfoItem* next = item->next;
        free(item->text);
        free(item);
        item = next;
    }

    g_info_head  = nullptr;
    g_info_tail  = nullptr;
    g_info_count = 0;
}

bool add_info_unlocked(const char* text)
{
    char* clone = clone_trimmed_string(text);
    if (!clone)
    {
        return false;
    }

    if (has_info_unlocked(clone))
    {
        free(clone);
        return true;
    }

    CallerInfoItem* item = static_cast<CallerInfoItem*>(calloc(1, sizeof(CallerInfoItem)));
    if (!item)
    {
        free(clone);
        return false;
    }

    item->text = clone;

    if (g_info_tail)
    {
        g_info_tail->next = item;
    }
    else
    {
        g_info_head = item;
    }

    g_info_tail = item;
    ++g_info_count;
    return true;
}

} // namespace

void reset()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_phone_state = {};
    clear_info_unlocked();
}

void onBluetoothConnectionEstablished(const char* friendlyName)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_phone_state = {};
    clear_info_unlocked();
    add_info_unlocked(friendlyName);
}

void onBluetoothDisconnected()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_phone_state = {};
}

void setCallStatus(int status)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_phone_state.call = status;
}

void setCallSetupStatus(int status)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_phone_state.callsetup = status;
}

void setCallHeldStatus(int status)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_phone_state.callheld = status;
}

void setScoAudioConnected(bool connected)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_phone_state.scoAudio = connected;
}

PhoneUiState uiState()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return ui_state_from_phone_state(g_phone_state);
}

PhoneState phoneState()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_phone_state;
}

const char* uiStateName(PhoneUiState state)
{
    switch (state)
    {
    case PhoneUiState::Idle:
        return "Idle";
    case PhoneUiState::IncomingCall:
        return "IncomingCall";
    case PhoneUiState::OutgoingCall:
        return "OutgoingCall";
    case PhoneUiState::ActiveCall:
        return "ActiveCall";
    case PhoneUiState::HeldCall:
        return "HeldCall";
    case PhoneUiState::AudioConnected:
        return "AudioConnected";
    default:
        return "Unknown";
    }
}

bool addCallerInfo(const char* text)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return add_info_unlocked(text);
}

void clearCallerInfo()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    clear_info_unlocked();
}

size_t getCallerInfoCnt()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_info_count;
}

const char* getCallerInfoAt(size_t index)
{
    std::lock_guard<std::mutex> lock(g_mutex);

    CallerInfoItem* item = g_info_head;
    while (item && index > 0)
    {
        item = item->next;
        --index;
    }

    return item ? item->text : nullptr;
}

} // namespace CallManager
