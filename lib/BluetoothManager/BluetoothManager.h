#pragma once

#include "thefly_common.h"

#include <stddef.h>
#include <stdint.h>

#include "esp_gap_bt_api.h"
#include "esp_hf_client_api.h"
#include "AudioManager.h"

class BtHostList;

namespace BtManager
{

static constexpr size_t kDeviceNameMaxLength = ESP_BT_GAP_MAX_BDNAME_LEN + 1;

enum class State
{
    Idle,
    Connecting,
    Connected,
    AudioAvailable,
    Reconnecting,
    Pairing,
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

bool init(const char*           deviceName    = nullptr,
          IncomingAudioCallback incomingAudio = AudioManager::hfp_incoming_audio,
          OutgoingAudioCallback outgoingAudio = AudioManager::hfp_outgoing_audio,
          const char*           pin           = nullptr);
bool initBluetoothOnly(const char* deviceName = nullptr, const char* pin = nullptr);
void setAudioCallbacks(IncomingAudioCallback incomingAudio = AudioManager::hfp_incoming_audio,
                       OutgoingAudioCallback outgoingAudio = AudioManager::hfp_outgoing_audio);
void poll();

bool        generateLegacyPinFromMac();
const char* generatedLegacyPin();

void        setPairedCallback(PairedCallback callback);
void        setStateChangedCallback(StateChangedCallback callback);
BtHostList& hostList();

Result connectToMac(const char* mac);
Result connectToMac(const esp_bd_addr_t mac);
Result startPairing();
Result disconnect();
Result disconnectNonConnectable();
Result shutdown();
Result setConnectableNonDiscoverable();
Result setNonConnectableNonDiscoverable();
Result pickupPhone();
bool   canNotifyOutgoingAudioReady();
void   notifyOutgoingAudioReady();

State                state();
const esp_bd_addr_t& connectedMac();
const PairedDevice&  lastPairedDevice();
bool                 hasLastPairedDevice();
bool                 isBonded(const esp_bd_addr_t mac);
bool                 isBonded(const char* mac);
const char*          localDeviceName();
bool                 localBdaddr(esp_bd_addr_t bdaddr);

const char* stateName(State value);
const char* resultName(Result value);

} // namespace BtManager
