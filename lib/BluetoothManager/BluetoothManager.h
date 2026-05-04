#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_gap_bt_api.h"
#include "esp_hf_client_api.h"
#include "AudioManager.h"

namespace BtManager
{

static constexpr size_t kDeviceNameMaxLength = ESP_BT_GAP_MAX_BDNAME_LEN + 1;

enum class State
{
    Idle,
    Connecting,
    Connected,
    Reconnecting,
    Pairing,
    WaitingForIncomingConnection,
};

enum class Result
{
    Ok,
    InvalidArgument,
    InitFailed,
    NotBonded,
    Busy,
    EspError,
};

struct PairedDevice
{
    esp_bd_addr_t mac                        = {};
    char          name[kDeviceNameMaxLength] = {};
};

using IncomingAudioCallback = esp_hf_client_incoming_data_cb_t;
using OutgoingAudioCallback = esp_hf_client_outgoing_data_cb_t;
using PairedCallback        = void (*)(const PairedDevice& device);
using StateChangedCallback  = void (*)(State state);

bool init(const char* deviceName = "The Fly", IncomingAudioCallback incomingAudio = AudioManager::hfp_incoming_audio, OutgoingAudioCallback outgoingAudio = AudioManager::hfp_outgoing_audio, const char* pin = nullptr);
void setAudioCallbacks(IncomingAudioCallback incomingAudio = AudioManager::hfp_incoming_audio, OutgoingAudioCallback outgoingAudio = AudioManager::hfp_outgoing_audio);

void setPairedCallback(PairedCallback callback);
void setStateChangedCallback(StateChangedCallback callback);

Result connectToMac(const char* mac);
Result connectToMac(const esp_bd_addr_t mac);
Result startPairing();
Result startWaitingForIncomingConnection();
Result disconnect();
Result pickupPhone();
void   notifyOutgoingAudioReady();

State                state();
const esp_bd_addr_t& connectedMac();
const PairedDevice&  lastPairedDevice();
bool                 hasLastPairedDevice();
bool                 isBonded(const esp_bd_addr_t mac);
bool                 isBonded(const char* mac);

const char* stateName(State value);
const char* resultName(Result value);

} // namespace BtManager
