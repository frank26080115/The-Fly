#include "WebCfgHandlers.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include <errno.h>
#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "Aegis.h"
#include "BtHostList.h"
#include "ClockAgent.h"
#include "IconLookup.h"
#include "WebServer.h"
#include "WifiManager.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "mbedtls/gcm.h"
#include "mbedtls/platform_util.h"
#include "utilfuncs.h"

extern WifiManager* wifi_manager;
extern BtHostList*  bt_host_list;

namespace WebCfgHandlers
{
namespace
{

constexpr const char* TAG = "WebCfgHandlers";
constexpr size_t      kGcmNonceSize = 12;
constexpr size_t      kGcmTagSize = 16;
constexpr size_t      kSetCfgMaxEncryptedSize = 64 * 1024;
constexpr uint8_t     kGetCfgMagic[] = { 'T', 'F', 'G', 'C' };
constexpr uint8_t     kGetCfgVersion = 2;
constexpr size_t      kGetCfgHeaderSize = sizeof(kGetCfgMagic) + 1;
constexpr uint8_t     kSetCfgMagic[] = { 'T', 'F', 'G', 'C' };
constexpr uint8_t     kSetCfgVersion = 1;
constexpr size_t      kSetCfgHeaderSize = sizeof(kSetCfgMagic) + 1 + kGcmNonceSize;
constexpr size_t      kSetCfgMinEnvelopeSize = kSetCfgHeaderSize + kGcmTagSize;
constexpr const char* kSetCfgErrorAttribute = "set_cfg_error";
constexpr const char* kSetCfgStatusAttribute = "set_cfg_status";
constexpr const char* kSetCfgStartedAttribute = "set_cfg_started";

void note_web_login()
{
    if (wifi_manager)
    {
        wifi_manager->noteWebLogin();
    }
}

void note_web_save()
{
    if (wifi_manager)
    {
        wifi_manager->noteWebSave();
    }
}

void note_web_error()
{
    if (wifi_manager)
    {
        wifi_manager->noteWebError();
    }
}

struct BinaryBlob
{
    uint8_t* data = nullptr;
    size_t   size = 0;

    ~BinaryBlob()
    {
        free(data);
    }
};

uint8_t* allocate_large_buffer(size_t size)
{
    if (size == 0)
    {
        size = 1;
    }

    void* buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer)
    {
        buffer = malloc(size);
    }
    return static_cast<uint8_t*>(buffer);
}

void set_cfg_error(AsyncWebServerRequest* request, int status_code, const char* message)
{
    if (!request || request->hasAttribute(kSetCfgErrorAttribute))
    {
        return;
    }

    request->setAttribute(kSetCfgErrorAttribute, message ? message : "Config update failed");
    request->setAttribute(kSetCfgStatusAttribute, static_cast<long>(status_code));
}

void send_set_cfg_error(AsyncWebServerRequest* request)
{
    if (!request)
    {
        return;
    }

    const String& error = request->getAttribute(kSetCfgErrorAttribute);
    const long    status_code = request->getAttribute(kSetCfgStatusAttribute, 500L);
    note_web_error();
    request->send(static_cast<int>(status_code), "text/plain", error.isEmpty() ? "Config update failed" : error);
}

bool cfg_mac_is_empty(const esp_bd_addr_t bdaddr)
{
    for (size_t i = 0; i < ESP_BD_ADDR_LEN; ++i)
    {
        if (bdaddr[i] != 0)
        {
            return false;
        }
    }
    return true;
}

bool parse_unix_time_text(const String& text, time_t& out)
{
    if (text.isEmpty())
    {
        return false;
    }

    const char* begin = text.c_str();
    char*       end   = nullptr;
    errno             = 0;
    const long long parsed = strtoll(begin, &end, 10);
    if (errno != 0 || end == begin || *end != '\0' || parsed <= 0)
    {
        return false;
    }

    out = static_cast<time_t>(parsed);
    return static_cast<long long>(out) == parsed;
}

bool read_unix_time_param(AsyncWebServerRequest* request, time_t& out, String& error)
{
    const AsyncWebParameter* param = WebServer::findRequestParam(request, "time");
    if (!param)
    {
        param = WebServer::findRequestParam(request, "current-time");
    }
    if (!param)
    {
        param = WebServer::findRequestParam(request, "unix_time");
    }
    if (!param)
    {
        error = "Missing Unix time";
        return false;
    }

    if (!parse_unix_time_text(param->value(), out))
    {
        error = "Unix time is invalid";
        return false;
    }
    return true;
}

bool time_sync_allowed(String& error)
{
    #if BUILD_WITH_SECURITY_LEVEL >= 2
    if (!wifi_manager || !wifi_manager->isGeneratedSoftApActive())
    {
        error = "Time sync is only available from the generated soft AP";
        return false;
    }
    #endif
    return true;
}

void append_json_comma(String& json, bool& first)
{
    if (first)
    {
        first = false;
        return;
    }
    json += ",";
}

void append_json_i64(String& json, long long value)
{
    char text[32] = {};
    snprintf(text, sizeof(text), "%lld", value);
    json += text;
}

void append_cfg_wifi_item(String& json, const wifi_item_t* item, bool& first)
{
    if (!item)
    {
        return;
    }

    const char* ssid = item->ssid;
    if (!ssid || ssid[0] == '\0')
    {
        return;
    }

    append_json_comma(json, first);
    json += "{\"ssid\":";
    json += WebServer::jsonString(ssid);
    json += ",\"icon\":";
    json += WebServer::jsonString(IconLookup::toString(item->icon));
    json += "}";
}

void append_cfg_cloud_item(String& json, const cloud_item_t* item, bool& first)
{
    if (!item)
    {
        return;
    }

    const char* url  = item->url;
    if (!url || url[0] == '\0')
    {
        return;
    }

    append_json_comma(json, first);
    json += "{\"url\":";
    json += WebServer::jsonString(url);
    json += ",\"icon\":";
    json += WebServer::jsonString(IconLookup::toString(item->icon));
    json += "}";
}

void append_cfg_bluetooth_host(String& json, const bt_host_item_t* item, bool& first)
{
    if (!item || cfg_mac_is_empty(item->bdaddr))
    {
        return;
    }

    char mac[18] = {};
    WebServer::formatMac(item->bdaddr, mac, sizeof(mac));

    append_json_comma(json, first);
    json += "{\"mac\":";
    json += WebServer::jsonString(mac);
    json += ",\"name_custom\":";
    json += WebServer::jsonString(item->name_custom);
    json += ",\"name_reported\":";
    json += WebServer::jsonString(item->name_reported);
    json += ",\"last_used\":";
    append_json_i64(json, static_cast<long long>(item->last_used));
    json += ",\"bonded\":";
    json += item->bonded ? "true" : "false";
    json += ",\"icon\":";
    json += WebServer::jsonString(IconLookup::toString(item->icon));
    json += "}";
}

bool build_cfg_json(String& json)
{
    json = "";
    if (!json.reserve(8192))
    {
        return false;
    }

    json += "{\"network\":{\"timezone\":";
    json += WebServer::jsonString(wifi_manager ? wifi_manager->timezone() : "");
    json += ",\"ntp_servers\":[";

    for (size_t i = 0; wifi_manager && i < WifiManager::kNtpServerCount; ++i)
    {
        if (i > 0)
        {
            json += ",";
        }
        json += WebServer::jsonString(wifi_manager->ntpServer(i));
    }

    json += "],\"stations\":[";
    bool first = true;
    for (size_t i = 0; wifi_manager && i < wifi_manager->stationCount(); ++i)
    {
        append_cfg_wifi_item(json, wifi_manager->station(i), first);
    }

    json += "],\"access_points\":[";
    first = true;
    for (size_t i = 0; wifi_manager && i < wifi_manager->accessPointCount(); ++i)
    {
        append_cfg_wifi_item(json, wifi_manager->accessPoint(i), first);
    }

    json += "],\"cloud_uploads\":[";
    first = true;
    for (size_t i = 0; wifi_manager && i < wifi_manager->cloudEndpointCount(); ++i)
    {
        append_cfg_cloud_item(json, wifi_manager->cloudEndpoint(i), first);
    }

    json += "]},\"bluetooth\":{\"hosts\":[";
    first = true;
    for (size_t i = 0; bt_host_list && i < bt_host_list->size(); ++i)
    {
        append_cfg_bluetooth_host(json, bt_host_list->get(i), first);
    }

    json += "]}}";
    return true;
}

bool encrypt_cfg_json_blob(const String& json,
                           const uint8_t session_key[WebServer::kSessionKeySize],
                           std::shared_ptr<BinaryBlob>& blob,
                           String& error)
{
    blob.reset(new BinaryBlob());
    if (!blob)
    {
        error = "Config response allocation failed";
        return false;
    }

    if (!session_key)
    {
        error = "Session key unavailable";
        return false;
    }

    const size_t plaintext_size = json.length();
    if (plaintext_size > kSetCfgMaxEncryptedSize - kGetCfgHeaderSize - kGcmTagSize)
    {
        error = "Config response is too large";
        return false;
    }

    blob->size = kGetCfgHeaderSize + plaintext_size + kGcmTagSize;
    blob->data = allocate_large_buffer(blob->size);
    if (!blob->data)
    {
        blob->size = 0;
        error = "Could not allocate encrypted config response";
        return false;
    }

    memcpy(blob->data, kGetCfgMagic, sizeof(kGetCfgMagic));
    blob->data[sizeof(kGetCfgMagic)] = kGetCfgVersion;

    uint8_t nonce[WebServer::kSessionGcmNonceSize] = {};
    WebServer::fillSessionNonce(WebServer::nextSessionNonceCounter(), nonce);
    uint8_t* ciphertext = blob->data + kGetCfgHeaderSize;
    uint8_t* tag        = ciphertext + plaintext_size;

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    const int key_result = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, session_key, WebServer::kSessionKeySize * 8);
    const int encrypt_result = key_result == 0
                                   ? mbedtls_gcm_crypt_and_tag(&gcm,
                                                              MBEDTLS_GCM_ENCRYPT,
                                                              plaintext_size,
                                                              nonce,
                                                              sizeof(nonce),
                                                              nullptr,
                                                              0,
                                                              reinterpret_cast<const uint8_t*>(json.c_str()),
                                                              ciphertext,
                                                              kGcmTagSize,
                                                              tag)
                                   : key_result;
    mbedtls_gcm_free(&gcm);
    mbedtls_platform_zeroize(nonce, sizeof(nonce));

    if (encrypt_result != 0)
    {
        blob.reset();
        error = "Config encryption failed";
        return false;
    }

    return true;
}

struct SetCfgUploadState
{
    AsyncWebServerRequest* request = nullptr;
    uint8_t* encrypted = nullptr;
    size_t expected_size = 0;
    size_t received_size = 0;
};

SetCfgUploadState g_set_cfg_upload;

void reset_set_cfg_upload(AsyncWebServerRequest* request = nullptr)
{
    if (request && g_set_cfg_upload.request != request)
    {
        return;
    }

    free(g_set_cfg_upload.encrypted);
    g_set_cfg_upload = {};
}

void set_field_error(String& error, const char* field, const char* problem)
{
    error = field ? field : "field";
    error += " ";
    error += problem ? problem : "is invalid";
}

bool copy_text_field(char* dst, size_t dst_size, const char* value, bool required, String& error, const char* field)
{
    if (!dst || dst_size == 0)
    {
        error = "Internal config storage is invalid";
        return false;
    }

    if (!value)
    {
        value = "";
    }
    if (required && value[0] == '\0')
    {
        set_field_error(error, field, "is required");
        return false;
    }
    if (strlcpy(dst, value, dst_size) >= dst_size)
    {
        set_field_error(error, field, "is too long");
        return false;
    }
    return true;
}

uint8_t parse_json_icon(JsonVariant value, uint8_t default_icon)
{
    if (!value.isNull() && value.is<int>())
    {
        const int icon = value.as<int>();
        if (icon > ICON_UNKNOWN && icon < ICON_LAST)
        {
            return static_cast<uint8_t>(icon);
        }
        return default_icon;
    }

    const char* icon_text = value.as<const char*>();
    if (icon_text && icon_text[0] != '\0')
    {
        const uint8_t parsed = IconLookup::fromString(icon_text);
        if (parsed > ICON_UNKNOWN && parsed < ICON_LAST)
        {
            return parsed;
        }
    }
    return default_icon;
}

bool valid_wifi_password(const char* password)
{
    const size_t length = password ? strlen(password) : 0;
    return length >= 8 && length < kNetworkConfigPasswordMaxLength;
}

const wifi_item_t* find_wifi_by_ssid(const wifi_item_t* items, size_t count, const char* ssid)
{
    if (!items || !ssid)
    {
        return nullptr;
    }

    for (size_t i = 0; i < count; ++i)
    {
        if (strcmp(items[i].ssid, ssid) == 0)
        {
            return &items[i];
        }
    }
    return nullptr;
}

const cloud_item_t* find_cloud_by_url(const cloud_item_t* items, size_t count, const char* url)
{
    if (!items || !url)
    {
        return nullptr;
    }

    for (size_t i = 0; i < count; ++i)
    {
        if (strcmp(items[i].url, url) == 0)
        {
            return &items[i];
        }
    }
    return nullptr;
}

bool parse_wifi_config_array(JsonObject network,
                             const char* key,
                             const wifi_item_t* existing,
                             size_t existing_count,
                             wifi_item_t* target,
                             size_t target_capacity,
                             uint8_t& target_count,
                             uint8_t default_icon,
                             String& error)
{
    JsonVariant array_value = network[key];
    if (array_value.isNull())
    {
        return true;
    }

    JsonArray array = array_value.as<JsonArray>();
    if (array.isNull())
    {
        set_field_error(error, key, "must be an array");
        return false;
    }
    if (array.size() > target_capacity)
    {
        set_field_error(error, key, "has too many entries");
        return false;
    }

    memset(target, 0, sizeof(wifi_item_t) * target_capacity);
    target_count = 0;

    for (JsonVariant value : array)
    {
        JsonObject item_json = value.as<JsonObject>();
        if (item_json.isNull())
        {
            set_field_error(error, key, "contains a non-object entry");
            return false;
        }

        wifi_item_t& item = target[target_count];
        const char* ssid = item_json["ssid"].as<const char*>();
        if (!copy_text_field(item.ssid, sizeof(item.ssid), ssid, true, error, "wifi ssid"))
        {
            return false;
        }

        const char* password = item_json["password"].as<const char*>();
        if (password && password[0] != '\0')
        {
            if (!valid_wifi_password(password) ||
                !copy_text_field(item.password, sizeof(item.password), password, true, error, "wifi password"))
            {
                error = "wifi password must be 8-63 characters";
                return false;
            }
        }
        else
        {
            const wifi_item_t* previous = find_wifi_by_ssid(existing, existing_count, item.ssid);
            if (!previous && target_count < existing_count)
            {
                previous = &existing[target_count];
            }
            if (!previous || !valid_wifi_password(previous->password))
            {
                error = "wifi password is required for new or previously-open networks";
                return false;
            }
            strlcpy(item.password, previous->password, sizeof(item.password));
        }

        item.icon = parse_json_icon(item_json["icon"], default_icon);
        ++target_count;
    }

    return true;
}

bool parse_cloud_config_array(JsonObject network,
                              const cloud_item_t* existing,
                              size_t existing_count,
                              cloud_item_t* target,
                              uint8_t& target_count,
                              String& error)
{
    JsonVariant array_value = network["cloud_uploads"];
    if (array_value.isNull())
    {
        return true;
    }

    JsonArray array = array_value.as<JsonArray>();
    if (array.isNull())
    {
        error = "cloud_uploads must be an array";
        return false;
    }
    if (array.size() > kNetworkConfigCloudAllowedEntries)
    {
        error = "cloud_uploads has too many entries";
        return false;
    }

    memset(target, 0, sizeof(cloud_item_t) * kNetworkConfigCloudMaxEntries);
    target_count = 0;

    for (JsonVariant value : array)
    {
        JsonObject item_json = value.as<JsonObject>();
        if (item_json.isNull())
        {
            error = "cloud_uploads contains a non-object entry";
            return false;
        }

        cloud_item_t& item = target[target_count];
        const char* url = item_json["url"].as<const char*>();
        if (!copy_text_field(item.url, sizeof(item.url), url, true, error, "cloud url"))
        {
            return false;
        }

        #if BUILD_WITH_SECURITY_LEVEL <= 0
        const char* password = item_json["password"].as<const char*>();
        if (password && password[0] != '\0')
        {
            if (!copy_text_field(item.password, sizeof(item.password), password, true, error, "cloud password"))
            {
                return false;
            }
        }
        else
        {
            const cloud_item_t* previous = find_cloud_by_url(existing, existing_count, item.url);
            if (!previous && target_count < existing_count)
            {
                previous = &existing[target_count];
            }
            if (!previous || previous->password[0] == '\0')
            {
                error = "cloud password is required for new cloud destinations";
                return false;
            }
            strlcpy(item.password, previous->password, sizeof(item.password));
        }
        #else
        item.password[0] = '\0';
        #endif
        item.icon = parse_json_icon(item_json["icon"], ICON_UNKNOWN);
        ++target_count;
    }

    return true;
}

bool validate_wifi_config_list(const wifi_item_t* items, size_t count, const char* label, String& error)
{
    for (size_t i = 0; i < count; ++i)
    {
        if (!items[i].ssid[0])
        {
            error = label;
            error += " contains a blank ssid";
            return false;
        }
        if (!valid_wifi_password(items[i].password))
        {
            error = label;
            error += " contains an open or invalid-password network";
            return false;
        }
    }
    return true;
}

bool parse_network_object(JsonObject network, const network_cfg_t& existing, network_cfg_t& staged, String& error)
{
    if (network.isNull())
    {
        error = "network must be an object";
        return false;
    }

    JsonVariant timezone = network["timezone"];
    if (!timezone.isNull() &&
        !copy_text_field(staged.timezone, sizeof(staged.timezone), timezone.as<const char*>(), true, error, "timezone"))
    {
        return false;
    }

    JsonVariant ntp_value = network["ntp_servers"];
    if (!ntp_value.isNull())
    {
        JsonArray ntp_servers = ntp_value.as<JsonArray>();
        if (ntp_servers.isNull())
        {
            error = "ntp_servers must be an array";
            return false;
        }
        if (ntp_servers.size() > kNetworkConfigNtpServerCount)
        {
            error = "ntp_servers has too many entries";
            return false;
        }

        size_t index = 0;
        for (JsonVariant server : ntp_servers)
        {
            char field_name[32] = {};
            snprintf(field_name, sizeof(field_name), "ntp_servers[%u]", static_cast<unsigned>(index));
            if (!copy_text_field(staged.ntp_server[index],
                                 sizeof(staged.ntp_server[index]),
                                 server.as<const char*>(),
                                 true,
                                 error,
                                 field_name))
            {
                return false;
            }
            ++index;
        }
    }

    return parse_wifi_config_array(network,
                                   "stations",
                                   existing.station,
                                   existing.station_count,
                                   staged.station,
                                   kNetworkConfigMaxEntries,
                                   staged.station_count,
                                   ICON_UNKNOWN,
                                   error) &&
           parse_wifi_config_array(network,
                                   "access_points",
                                   existing.access_point,
                                   existing.access_point_count,
                                   staged.access_point,
                                   kNetworkConfigAllowedEntriesAP,
                                   staged.access_point_count,
                                   ICON_UNKNOWN,
                                   error) &&
           parse_cloud_config_array(network,
                                    existing.cloud,
                                    existing.cloud_endpoint_count,
                                    staged.cloud,
                                    staged.cloud_endpoint_count,
                                    error) &&
           validate_wifi_config_list(staged.station, staged.station_count, "stations", error) &&
           validate_wifi_config_list(staged.access_point, staged.access_point_count, "access_points", error);
}

bool mac_is_empty(const esp_bd_addr_t bdaddr)
{
    const esp_bd_addr_t empty = {};
    return bda_equal(bdaddr, empty);
}

const bt_host_item_t* find_bt_host_by_mac(const bt_host_list_t& hosts, const esp_bd_addr_t bdaddr)
{
    for (size_t i = 0; i < hosts.count; ++i)
    {
        if (bda_equal(hosts.host[i].bdaddr, bdaddr))
        {
            return &hosts.host[i];
        }
    }
    return nullptr;
}

bool copy_optional_host_name(JsonObject host,
                             const char* key,
                             char* dst,
                             size_t dst_size,
                             const char* fallback,
                             String& error)
{
    const char* value = fallback ? fallback : "";
    if (!host[key].isNull())
    {
        value = host[key].as<const char*>();
    }
    return copy_text_field(dst, dst_size, value, false, error, key);
}

bool parse_bluetooth_object(JsonObject bluetooth, const bt_host_list_t& existing, bt_host_list_t& staged, String& error)
{
    if (bluetooth.isNull())
    {
        error = "bluetooth must be an object";
        return false;
    }

    JsonVariant hosts_value = bluetooth["hosts"];
    if (hosts_value.isNull())
    {
        return true;
    }

    JsonArray hosts = hosts_value.as<JsonArray>();
    if (hosts.isNull())
    {
        error = "bluetooth hosts must be an array";
        return false;
    }

    memset(&staged, 0, sizeof(staged));
    staged.count = 0;

    for (JsonVariant value : hosts)
    {
        JsonObject host_json = value.as<JsonObject>();
        if (host_json.isNull())
        {
            error = "bluetooth hosts contains a non-object entry";
            return false;
        }

        const char* mac_text = host_json["mac"].as<const char*>();
        if (!mac_text || mac_text[0] == '\0')
        {
            continue;
        }

        esp_bd_addr_t bdaddr = {};
        if (!parse_mac(mac_text, bdaddr))
        {
            error = "bluetooth host mac is invalid";
            return false;
        }
        if (mac_is_empty(bdaddr))
        {
            continue;
        }
        if (staged.count >= kBtHostListMaxEntries)
        {
            error = "bluetooth hosts has too many entries";
            return false;
        }

        const bt_host_item_t* previous = find_bt_host_by_mac(existing, bdaddr);
        bt_host_item_t& item = staged.host[staged.count];
        copy_bda(item.bdaddr, bdaddr);

        const char* custom_fallback = previous ? previous->name_custom : "";
        if (!host_json["name"].isNull() && host_json["name_custom"].isNull())
        {
            custom_fallback = host_json["name"].as<const char*>();
        }
        if (!copy_optional_host_name(host_json, "name_custom", item.name_custom, sizeof(item.name_custom), custom_fallback, error) ||
            !copy_optional_host_name(host_json,
                                     "name_reported",
                                     item.name_reported,
                                     sizeof(item.name_reported),
                                     previous ? previous->name_reported : "",
                                     error))
        {
            return false;
        }

        if (!host_json["last_used"].isNull())
        {
            const long long last_used = host_json["last_used"].as<long long>();
            if (last_used < 0)
            {
                error = "bluetooth host last_used is invalid";
                return false;
            }
            item.last_used = static_cast<time_t>(last_used);
        }
        else if (previous)
        {
            item.last_used = previous->last_used;
        }

        item.bonded = previous ? previous->bonded : false;
        item.icon = parse_json_icon(host_json["icon"], previous ? previous->icon : ICON_UNKNOWN);
        ++staged.count;
    }

    return true;
}

#if BUILD_WITH_SECURITY_LEVEL >= 1
bool decrypt_set_cfg_blob(const uint8_t* encrypted,
                          size_t encrypted_size,
                          uint8_t*& plaintext,
                          size_t& plaintext_size,
                          int& status_code,
                          String& error)
{
    plaintext = nullptr;
    plaintext_size = 0;

    if (!encrypted || encrypted_size < kSetCfgMinEnvelopeSize)
    {
        status_code = 400;
        error = "Encrypted config body is too small";
        return false;
    }
    if (memcmp(encrypted, kSetCfgMagic, sizeof(kSetCfgMagic)) != 0)
    {
        status_code = 400;
        error = "Encrypted config magic is invalid";
        return false;
    }
    if (encrypted[sizeof(kSetCfgMagic)] != kSetCfgVersion)
    {
        status_code = 400;
        error = "Encrypted config version is unsupported";
        return false;
    }
    if (!Aegis::isInitialized() && !Aegis::init())
    {
        status_code = 500;
        error = "Network key unavailable";
        return false;
    }

    const uint8_t* network_key = Aegis::getNetworkKey();
    if (!network_key)
    {
        status_code = 500;
        error = "Network key unavailable";
        return false;
    }

    const uint8_t* nonce = encrypted + sizeof(kSetCfgMagic) + 1;
    const uint8_t* ciphertext = encrypted + kSetCfgHeaderSize;
    const uint8_t* tag = encrypted + encrypted_size - kGcmTagSize;
    plaintext_size = encrypted_size - kSetCfgHeaderSize - kGcmTagSize;

    plaintext = allocate_large_buffer(plaintext_size + 1);
    if (!plaintext)
    {
        status_code = 500;
        error = "Could not allocate config plaintext buffer";
        return false;
    }

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    const int key_result = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, network_key, Aegis::kNetworkKeySize * 8);
    const int decrypt_result = key_result == 0
                                   ? mbedtls_gcm_auth_decrypt(&gcm,
                                                              plaintext_size,
                                                              nonce,
                                                              kGcmNonceSize,
                                                              nullptr,
                                                              0,
                                                              tag,
                                                              kGcmTagSize,
                                                              ciphertext,
                                                              plaintext)
                                   : key_result;
    mbedtls_gcm_free(&gcm);

    if (decrypt_result != 0)
    {
        free(plaintext);
        plaintext = nullptr;
        plaintext_size = 0;
        status_code = 400;
        error = "Config decryption failed";
        return false;
    }

    plaintext[plaintext_size] = '\0';
    return true;
}
#endif

bool apply_set_cfg_json(const uint8_t* plaintext, size_t plaintext_size, int& status_code, String& error)
{
    JsonDocument doc;
    const DeserializationError json_error =
        deserializeJson(doc, reinterpret_cast<const char*>(plaintext), plaintext_size);
    if (json_error)
    {
        status_code = 400;
        error = "Config JSON parse failed: ";
        error += json_error.c_str();
        return false;
    }

    JsonObject root = doc.as<JsonObject>();
    if (root.isNull())
    {
        status_code = 400;
        error = "Config JSON root must be an object";
        return false;
    }

    const bool has_network = !root["network"].isNull();
    const bool has_bluetooth = !root["bluetooth"].isNull();
    if (!has_network && !has_bluetooth)
    {
        status_code = 400;
        error = "Config JSON must include network or bluetooth";
        return false;
    }

    network_cfg_t network_existing = {};
    network_cfg_t network_staged = {};
    bt_host_list_t bluetooth_staged = {};
    bool network_config_changed = false;

    if (has_network)
    {
        if (!wifi_manager || !wifi_manager->copyConfig(network_existing))
        {
            status_code = 500;
            error = "Wi-Fi config is unavailable";
            return false;
        }
        network_staged = network_existing;
        if (!parse_network_object(root["network"].as<JsonObject>(), network_existing, network_staged, error))
        {
            status_code = 400;
            return false;
        }

        uint8_t existing_hash[Aegis::kSha1Size] = {};
        uint8_t staged_hash[Aegis::kSha1Size] = {};
        if (!Aegis::networkConfigHash(&network_existing, sizeof(network_existing), existing_hash) ||
            !Aegis::networkConfigHash(&network_staged, sizeof(network_staged), staged_hash))
        {
            mbedtls_platform_zeroize(existing_hash, sizeof(existing_hash));
            mbedtls_platform_zeroize(staged_hash, sizeof(staged_hash));
            status_code = 500;
            error = "Wi-Fi config hash failed";
            return false;
        }

        network_config_changed = memcmp(existing_hash, staged_hash, sizeof(existing_hash)) != 0;
        mbedtls_platform_zeroize(existing_hash, sizeof(existing_hash));
        mbedtls_platform_zeroize(staged_hash, sizeof(staged_hash));
    }

    if (has_bluetooth)
    {
        if (!bt_host_list || !bt_host_list->copyHostList(bluetooth_staged))
        {
            status_code = 500;
            error = "Bluetooth config is unavailable";
            return false;
        }

        const bt_host_list_t bluetooth_existing = bluetooth_staged;
        if (!parse_bluetooth_object(root["bluetooth"].as<JsonObject>(), bluetooth_existing, bluetooth_staged, error))
        {
            status_code = 400;
            return false;
        }
    }

    if (has_network && !wifi_manager->replaceConfig(network_staged))
    {
        status_code = 500;
        error = "Wi-Fi config save failed";
        return false;
    }
    if (has_network)
    {
        if (!Aegis::cacheNetworkConfigHash(&network_staged, sizeof(network_staged)))
        {
            status_code = 500;
            error = "Wi-Fi config hash cache update failed";
            return false;
        }
        #if BUILD_WITH_SECURITY_LEVEL >= 2
        if (network_config_changed && !Aegis::generateFilecryptKey())
        {
            status_code = 500;
            error = "Filecrypt-key regeneration failed";
            return false;
        }
        if (network_config_changed)
        {
            ESP_LOGI(TAG, "regenerated filecrypt-key after network config change");
        }
        #endif
    }
    if (has_bluetooth && !bt_host_list->replaceHostList(bluetooth_staged))
    {
        status_code = 500;
        error = "Bluetooth config save failed";
        return false;
    }

    status_code = 200;
    return true;
}

bool process_set_cfg_blob(const uint8_t* encrypted, size_t encrypted_size, int& status_code, String& error)
{
    #if BUILD_WITH_SECURITY_LEVEL <= 0
    return apply_set_cfg_json(encrypted, encrypted_size, status_code, error);
    #else
    uint8_t* plaintext = nullptr;
    size_t plaintext_size = 0;
    if (!decrypt_set_cfg_blob(encrypted, encrypted_size, plaintext, plaintext_size, status_code, error))
    {
        return false;
    }

    const bool ok = apply_set_cfg_json(plaintext, plaintext_size, status_code, error);
    free(plaintext);
    return ok;
    #endif
}

bool begin_set_cfg_upload(AsyncWebServerRequest* request, size_t total)
{
    request->setAttribute(kSetCfgStartedAttribute, true);
    request->onDisconnect([]() {
        reset_set_cfg_upload();
    });

    const size_t expected_size = total != 0 ? total : request->contentLength();
    if (expected_size == 0)
    {
        set_cfg_error(request, 400, "Missing encrypted config body");
        return false;
    }
    if (expected_size > kSetCfgMaxEncryptedSize)
    {
        set_cfg_error(request, 413, "Encrypted config body is too large");
        return false;
    }
    #if BUILD_WITH_SECURITY_LEVEL >= 1
    if (expected_size < kSetCfgMinEnvelopeSize)
    {
        set_cfg_error(request, 400, "Encrypted config body is too small");
        return false;
    }
    #endif
    if (g_set_cfg_upload.request && g_set_cfg_upload.request != request)
    {
        set_cfg_error(request, 409, "Another config update is already in progress");
        return false;
    }

    reset_set_cfg_upload(request);
    g_set_cfg_upload.encrypted = allocate_large_buffer(expected_size);
    if (!g_set_cfg_upload.encrypted)
    {
        set_cfg_error(request, 500, "Could not allocate encrypted config buffer");
        return false;
    }

    g_set_cfg_upload.request = request;
    g_set_cfg_upload.expected_size = expected_size;
    g_set_cfg_upload.received_size = 0;
    return true;
}

} // namespace

void sendCfg(AsyncWebServerRequest* request)
{
    if (!request)
    {
        return;
    }

    #if BUILD_WITH_SECURITY_LEVEL <= 0
    String json;
    if (!build_cfg_json(json))
    {
        note_web_error();
        request->send(500, "text/plain", "Config JSON allocation failed");
        return;
    }

    request->send(200, "application/json", json);
    #else
    uint8_t session_key[WebServer::kSessionKeySize] = {};
    const WebServer::SessionAuthResult auth = WebServer::authenticateSessionRequest(request, session_key);
    if (auth != WebServer::SessionAuthResult::Ok)
    {
        note_web_error();
        request->send(401, "text/plain", WebServer::sessionAuthResultName(auth));
        return;
    }

    String json;
    if (!build_cfg_json(json))
    {
        note_web_error();
        request->send(500, "text/plain", "Config JSON allocation failed");
        return;
    }

    std::shared_ptr<BinaryBlob> blob;
    String error;
    if (!encrypt_cfg_json_blob(json, session_key, blob, error))
    {
        mbedtls_platform_zeroize(session_key, sizeof(session_key));
        note_web_error();
        request->send(500, "text/plain", error.isEmpty() ? "Config encryption failed" : error);
        return;
    }
    mbedtls_platform_zeroize(session_key, sizeof(session_key));

    AsyncWebServerResponse* response = request->beginResponse(
        "application/octet-stream",
        blob->size,
        [blob](uint8_t* buffer, size_t max_len, size_t index) -> size_t {
            if (!blob || !blob->data || index >= blob->size)
            {
                return 0;
            }

            const size_t remaining = blob->size - index;
            const size_t to_copy   = remaining < max_len ? remaining : max_len;
            memcpy(buffer, blob->data + index, to_copy);
            return to_copy;
        });
    if (!response)
    {
        note_web_error();
        request->send(500);
        return;
    }

    note_web_login();
    response->addHeader("X-TheFly-Content", "config-json");
    response->addHeader("X-TheFly-Encryption", "aes-256-gcm-session-v1");
    response->addHeader("X-TheFly-Blob-Format", "magic4|version1|ciphertext|tag16");
    request->send(response);
    #endif
}

void writeSetCfgBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total)
{
    if (!request || len == 0)
    {
        return;
    }

    if (index == 0 && !begin_set_cfg_upload(request, total))
    {
        return;
    }
    if (request->hasAttribute(kSetCfgErrorAttribute))
    {
        return;
    }
    if (g_set_cfg_upload.request != request || !g_set_cfg_upload.encrypted)
    {
        set_cfg_error(request, 500, "Config upload state lost");
        return;
    }
    if (index + len > g_set_cfg_upload.expected_size)
    {
        set_cfg_error(request, 400, "Encrypted config body length mismatch");
        reset_set_cfg_upload(request);
        return;
    }

    memcpy(g_set_cfg_upload.encrypted + index, data, len);
    const size_t received_end = index + len;
    if (received_end > g_set_cfg_upload.received_size)
    {
        g_set_cfg_upload.received_size = received_end;
    }
}

void finishSetCfg(AsyncWebServerRequest* request)
{
    if (!request)
    {
        return;
    }

    if (request->hasAttribute(kSetCfgErrorAttribute))
    {
        reset_set_cfg_upload(request);
        send_set_cfg_error(request);
        return;
    }
    if (!request->hasAttribute(kSetCfgStartedAttribute))
    {
        set_cfg_error(request, 400, "Missing encrypted config body");
        send_set_cfg_error(request);
        return;
    }
    if (g_set_cfg_upload.request != request || !g_set_cfg_upload.encrypted)
    {
        set_cfg_error(request, 500, "Config upload state lost");
        send_set_cfg_error(request);
        return;
    }
    if (g_set_cfg_upload.received_size != g_set_cfg_upload.expected_size)
    {
        set_cfg_error(request, 400, "Encrypted config body is incomplete");
        reset_set_cfg_upload(request);
        send_set_cfg_error(request);
        return;
    }

    int status_code = 500;
    String error;
    const bool ok = process_set_cfg_blob(g_set_cfg_upload.encrypted,
                                         g_set_cfg_upload.expected_size,
                                         status_code,
                                         error);
    reset_set_cfg_upload(request);

    if (!ok)
    {
        note_web_error();
        request->send(status_code, "text/plain", error.isEmpty() ? "Config update failed" : error);
        return;
    }

    note_web_save();
    ESP_LOGI(TAG, "updated config from encrypted web upload");
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

void timeSync(AsyncWebServerRequest* request)
{
    if (!request)
    {
        return;
    }

    String error;
    if (!time_sync_allowed(error))
    {
        note_web_error();
        request->send(403, "text/plain", error);
        return;
    }

    time_t unix_time = 0;
    if (!read_unix_time_param(request, unix_time, error))
    {
        note_web_error();
        request->send(400, "text/plain", error);
        return;
    }

    if (!Clock.setUnixTime(unix_time))
    {
        note_web_error();
        request->send(400, "text/plain", "Unix time is outside the RTC-supported range");
        return;
    }

    time_t verified_time = 0;
    if (!Clock.getUnixTime(&verified_time))
    {
        note_web_error();
        request->send(500, "text/plain", "Time sync failed");
        return;
    }

    note_web_save();
    String json;
    json.reserve(48);
    json += "{\"status\":\"ok\",\"current-time\":";
    append_json_i64(json, static_cast<long long>(verified_time));
    json += "}";
    request->send(200, "application/json", json);
}

} // namespace WebCfgHandlers
