#include "WebServer.h"

#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include <esp_system.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "Aegis.h"
#include "BluetoothManager.h"
#include "BtHostList.h"
#include "ClockAgent.h"
#include "DiskStats.h"
#if defined(BUILD_FTP_SERVER) && BUILD_WITH_SECURITY_LEVEL <= 0
#include "FtpServer.h"
#endif
#include "MicroSdCard.h"
#include "ModalDialog.h"
#include "WebCfgHandlers.h"
#include "WebFileHandlers.h"
#include "WifiManager.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/platform_util.h"
#include "nvs.h"
#include "sprites.h"
#include "thefly_version.h"
#include "utilfuncs.h"
#include "web_assets.h"

extern WifiManager* wifi_manager;
extern BtHostList*  bt_host_list;
extern FlyGui*      gui;
extern ModalDialog* get_modal_dialog();

namespace
{

constexpr const char* TAG = "WebServer";
constexpr const char* kHeaderSessionSaltFromClient = "X-TheFly-Session-Salt-From-Client";
constexpr const char* kHeaderSessionResponseFromClient = "X-TheFly-Session-Response-From-Client";
constexpr const char* kBluedroidNvsNamespace = "bt_config.conf";
constexpr const char* kBtHostListNvsNamespace = "bt_hosts";
constexpr const char* kNetworkNvsNamespace = "wifi_cfg";
constexpr uint16_t    kWebServerPort = 80;

#if defined(BUILD_FTP_SERVER) && BUILD_WITH_SECURITY_LEVEL <= 0
// This is only a login gate for plain FTP. Replace these credentials before
// exposing FTP; this library is not SFTP and does not encrypt its traffic.
constexpr const char* kFtpUser     = "thefly";
constexpr const char* kFtpPassword = "replace-me";
#endif

AsyncWebServer g_server(kWebServerPort);
bool           g_initialized = false;
WebServer::SessionSecurityState g_session_security;

void zero_session_key()
{
    mbedtls_platform_zeroize(g_session_security.session_key, sizeof(g_session_security.session_key));
    g_session_security.session_key_valid = false;
}

void reset_session_security()
{
    mbedtls_platform_zeroize(&g_session_security, sizeof(g_session_security));
}

void note_web_page_load()
{
    if (wifi_manager)
    {
        wifi_manager->noteWebPageLoad();
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

void make_mdns_hostname(char* out, size_t out_size)
{
    if (!out || out_size == 0)
    {
        return;
    }

    uint8_t mac[6] = {};
    WiFi.macAddress(mac);
    snprintf(out, out_size, "the-fly-%02x%02x", mac[4], mac[5]);
}

void start_mdns()
{
    char hostname[24] = {};
    make_mdns_hostname(hostname, sizeof(hostname));
    if (hostname[0] == '\0')
    {
        ESP_LOGW(TAG, "mDNS hostname generation failed");
        return;
    }

    if (!MDNS.begin(hostname))
    {
        ESP_LOGW(TAG, "mDNS start failed for %s.local", hostname);
        return;
    }

    MDNS.addService("http", "tcp", kWebServerPort);
    MDNS.addServiceTxt("http", "tcp", "device", "the-fly");
    MDNS.addServiceTxt("http", "tcp", "path", "/");
    MDNS.addService("the-fly", "tcp", kWebServerPort);
    MDNS.addServiceTxt("the-fly", "tcp", "path", "/");
    MDNS.addServiceTxt("the-fly", "tcp", "version", version_str ? version_str : "");
    ESP_LOGI(TAG, "mDNS started: http://%s.local/ service=_the-fly._tcp", hostname);
}

void reboot_after_reset_dialog()
{
    ESP_LOGI(TAG, "reset confirmation dismissed; rebooting");
    delay(50);
    esp_restart();
}

void show_reset_reboot_dialog(const char* reset_name, const char* text)
{
    ModalDialog* dialog = get_modal_dialog();
    if (!dialog || !gui)
    {
        ESP_LOGW(TAG, "%s succeeded, but modal dialog is unavailable", reset_name);
        return;
    }

    dialog->configure(sprite_thumbsup_100,
                      SPRITE_THUMBSUP_100_BYTES,
                      SPRITE_THUMBSUP_100_WIDTH,
                      SPRITE_THUMBSUP_100_HEIGHT,
                      text,
                      FLYGUI_VIEW_MODAL_DIALOG,
                      reboot_after_reset_dialog);
    if (!gui->showView(FLYGUI_VIEW_MODAL_DIALOG))
    {
        ESP_LOGW(TAG, "%s succeeded, but modal dialog could not be shown", reset_name);
    }
}

void show_password_reset_reboot_dialog()
{
    show_reset_reboot_dialog("password reset", "Password reset successful\nDismiss to reboot.");
}

void show_memory_reset_reboot_dialog()
{
    show_reset_reboot_dialog("memory reset", "Memory reset successful\nDismiss to reboot.");
}

bool read_hex_key_param(AsyncWebServerRequest* request, const char* name, uint8_t* key, size_t key_size, String& error)
{
    if (!key || key_size == 0)
    {
        error = "Internal key buffer is invalid";
        return false;
    }

    mbedtls_platform_zeroize(key, key_size);
    const AsyncWebParameter* param = WebServer::findRequestParam(request, name);
    if (!param)
    {
        error = "Missing ";
        error += name ? name : "key";
        return false;
    }

    const String& value = param->value();
    if (!hex_to_bytes(value, key, key_size))
    {
        error = "Invalid ";
        error += name ? name : "key";
        return false;
    }

    return true;
}

bool constant_time_equal(const uint8_t* lhs, const uint8_t* rhs, size_t size)
{
    if (!lhs || !rhs)
    {
        return false;
    }

    uint8_t diff = 0;
    for (size_t i = 0; i < size; ++i)
    {
        diff |= lhs[i] ^ rhs[i];
    }
    return diff == 0;
}

bool load_network_key(const uint8_t*& network_key)
{
    network_key = nullptr;
    if (!Aegis::isInitialized() && !Aegis::init())
    {
        return false;
    }
    network_key = Aegis::getNetworkKey();
    return network_key != nullptr;
}

bool compute_hmac(const uint8_t* key, const uint8_t* data, size_t data_size, uint8_t out[Aegis::kSha256Size])
{
    return Aegis::hmacSha256(key, Aegis::kNetworkKeySize, data, data_size, out);
}

bool begin_new_session(const String& client_salt_hex)
{
    reset_session_security();

    esp_fill_random(g_session_security.session_challenge, sizeof(g_session_security.session_challenge));
    esp_fill_random(g_session_security.session_salt_from_server, sizeof(g_session_security.session_salt_from_server));
    g_session_security.challenge_valid = true;
    g_session_security.nonce_counter = 0;

    if (!client_salt_hex.isEmpty())
    {
        hex_to_bytes(client_salt_hex, g_session_security.session_salt_from_client, sizeof(g_session_security.session_salt_from_client));
    }

    const uint8_t* network_key = nullptr;
    if (!load_network_key(network_key))
    {
        return false;
    }

    if (!compute_hmac(network_key,
                      g_session_security.session_challenge,
                      sizeof(g_session_security.session_challenge),
                      g_session_security.session_response_from_client))
    {
        return false;
    }

    if (!compute_hmac(network_key,
                      g_session_security.session_response_from_client,
                      sizeof(g_session_security.session_response_from_client),
                      g_session_security.session_response_from_server))
    {
        return false;
    }

    g_session_security.response_valid = true;
    return true;
}

const web_asset_desc_t* find_asset_by_name(const char* file_name)
{
    if (!file_name || file_name[0] == '\0')
    {
        return nullptr;
    }

    for (size_t i = 0; i < WEB_ASSETS_CNT; ++i)
    {
        if (strcmp(web_assets_list[i].file_name, file_name) == 0)
        {
            return &web_assets_list[i];
        }
    }

    return nullptr;
}

const web_asset_desc_t* find_asset_for_url(const String& url)
{
    if (url == "/")
    {
        return find_asset_by_name("index.html");
    }

    const char* path = url.c_str();
    while (*path == '/')
    {
        ++path;
    }
    return find_asset_by_name(path);
}

String asset_route(const web_asset_desc_t& asset)
{
    String route("/");
    route += asset.file_name;
    return route;
}

void send_asset(AsyncWebServerRequest* request)
{
    if (!request)
    {
        return;
    }

    const web_asset_desc_t* asset = find_asset_for_url(request->url());
    if (!asset || !asset->ptr_payload || asset->compressed_size == 0)
    {
        request->send(404);
        return;
    }

    AsyncWebServerResponse* response =
        request->beginResponse(200, asset->mime_type, asset->ptr_payload, asset->compressed_size);
    if (!response)
    {
        request->send(500);
        return;
    }

    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
}

void append_json_u64(String& json, uint64_t value)
{
    char text[24] = {};
    snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(value));
    json += text;
}

void append_json_i64(String& json, int64_t value)
{
    char text[32] = {};
    snprintf(text, sizeof(text), "%lld", static_cast<long long>(value));
    json += text;
}

bool ip_is_zero(const IPAddress& ip)
{
    return static_cast<uint32_t>(ip) == 0;
}

String self_ip_string()
{
    const IPAddress station_ip = WiFi.localIP();
    if (!ip_is_zero(station_ip))
    {
        return station_ip.toString();
    }

    const IPAddress soft_ap_ip = WiFi.softAPIP();
    return ip_is_zero(soft_ap_ip) ? String("") : soft_ap_ip.toString();
}

void send_info(AsyncWebServerRequest* request)
{
    if (!request)
    {
        return;
    }

    note_web_page_load();
    const bool security_ready = begin_new_session(request->header(kHeaderSessionSaltFromClient));

    esp_bd_addr_t bdaddr = {};
    char          bdaddr_text[18] = "unknown";
    if (BtManager::localBdaddr(bdaddr))
    {
        WebServer::formatMac(bdaddr, bdaddr_text, sizeof(bdaddr_text));
    }

    uint64_t total_bytes = 0;
    uint64_t free_bytes  = 0;
    const bool sd_ready = MicroSdCard::isReady();
    const bool disk_ready = sd_ready && DiskStats::diskSpace(total_bytes, free_bytes);
    const uint64_t used_bytes = total_bytes > free_bytes ? total_bytes - free_bytes : 0;
    const char* disk_health = !sd_ready ? MicroSdCard::healthName(MicroSdCard::Health::NotReady) :
                              !disk_ready ? "Unknown" :
                              free_bytes == 0 ? MicroSdCard::healthName(MicroSdCard::Health::Full) :
                              MicroSdCard::healthName(MicroSdCard::Health::Ready);
    const bool default_soft_ap = wifi_manager && wifi_manager->isGeneratedSoftApActive();
    time_t current_time = 0;
    const bool current_time_valid = Clock.getUnixTime(&current_time);

    String session_challenge_hex;
    String session_response_hex;
    String session_salt_hex;
    session_challenge_hex.reserve(WebServer::kSessionChallengeSize * 2);
    session_response_hex.reserve(WebServer::kSessionResponseSize * 2);
    session_salt_hex.reserve(WebServer::kSessionSaltHalfSize * 2);
    bytes_to_hex(g_session_security.session_challenge, sizeof(g_session_security.session_challenge), session_challenge_hex);
    bytes_to_hex(g_session_security.session_response_from_server, sizeof(g_session_security.session_response_from_server), session_response_hex);
    bytes_to_hex(g_session_security.session_salt_from_server, sizeof(g_session_security.session_salt_from_server), session_salt_hex);

    String json;
    json.reserve(1100);
    json += "{";
    json += "\"device_name\":";
    json += WebServer::jsonString(BtManager::localDeviceName());
    json += ",\"bdaddr\":";
    json += WebServer::jsonString(bdaddr_text);
    json += ",\"wifi_mac\":";
    json += WebServer::jsonString(WiFi.macAddress().c_str());
    json += ",\"self_ip\":";
    json += WebServer::jsonString(self_ip_string().c_str());
    json += ",\"default_soft_ap\":";
    json += default_soft_ap ? "true" : "false";
    json += ",\"security_ready\":";
    json += security_ready ? "true" : "false";
    json += ",\"security-level\":";
    json += BUILD_WITH_SECURITY_LEVEL;
    json += ",\"current-time\":";
    if (current_time_valid)
    {
        append_json_i64(json, static_cast<int64_t>(current_time));
    }
    else
    {
        json += "null";
    }
    json += ",\"config_limits\":{";
    json += "\"stations\":";
    append_json_u64(json, kNetworkConfigMaxEntries);
    json += ",\"access_points\":";
    append_json_u64(json, kNetworkConfigAllowedEntriesAP);
    json += ",\"cloud_uploads\":";
    #ifdef BUILD_CLOUD_FEATURES
    append_json_u64(json, kNetworkConfigCloudAllowedEntries);
    #else
    json += "false";
    #endif
    json += ",\"bluetooth_hosts\":";
    append_json_u64(json, kBtHostListMaxEntries);
    json += ",\"ntp_servers\":";
    append_json_u64(json, WifiManager::kNtpServerCount);
    json += "}";
    json += ",\"session_challenge\":";
    json += WebServer::jsonString(session_challenge_hex.c_str());
    json += ",\"session_response_from_server\":";
    json += WebServer::jsonString(session_response_hex.c_str());
    json += ",\"session_salt_from_server\":";
    json += WebServer::jsonString(session_salt_hex.c_str());
    json += ",\"firmware\":";
    json += WebServer::jsonString(version_str);
    json += ",\"compiler\":";
    json += WebServer::jsonString(compiler_version_str);
    json += ",\"compiled\":";
    json += WebServer::jsonString(compiler_time_str);
    json += ",\"disk\":{";
    json += "\"ready\":";
    json += disk_ready ? "true" : "false";
    json += ",\"health\":";
    json += WebServer::jsonString(disk_health);
    json += ",\"total_bytes\":";
    append_json_u64(json, total_bytes);
    json += ",\"used_bytes\":";
    append_json_u64(json, used_bytes);
    json += ",\"free_bytes\":";
    append_json_u64(json, free_bytes);
    json += "}}";

    request->send(200, "application/json", json);
}

void reset_password(AsyncWebServerRequest* request)
{
    if (!request)
    {
        return;
    }

    #if BUILD_WITH_SECURITY_LEVEL <= 0
    note_web_error();
    request->send(501, "text/plain", "Password reset is not implemented for security level 0");
    #else
    if (!wifi_manager || !wifi_manager->isGeneratedSoftApActive())
    {
        note_web_error();
        request->send(403, "text/plain", "Password reset is only available from the generated soft AP");
        return;
    }

    uint8_t network_key[Aegis::kNetworkKeySize] = {};
    String  error;
    if (!read_hex_key_param(request, "network_key", network_key, sizeof(network_key), error))
    {
        note_web_error();
        request->send(400, "text/plain", error);
        return;
    }

    bool ok = false;
    #if BUILD_WITH_SECURITY_LEVEL == 1
    uint8_t filecrypt_key[Aegis::kFilecryptKeySize] = {};
    if (!read_hex_key_param(request, "filecrypt_key", filecrypt_key, sizeof(filecrypt_key), error))
    {
        mbedtls_platform_zeroize(network_key, sizeof(network_key));
        note_web_error();
        request->send(400, "text/plain", error);
        return;
    }

    ok = Aegis::setMasterKeys(filecrypt_key, network_key);
    mbedtls_platform_zeroize(filecrypt_key, sizeof(filecrypt_key));
    #else
    ok = Aegis::setNetworkKeyAndGenerateFilecryptKey(network_key);
    #endif

    mbedtls_platform_zeroize(network_key, sizeof(network_key));
    if (!ok)
    {
        note_web_error();
        request->send(500, "text/plain", "Password reset failed");
        return;
    }

    note_web_save();
    reset_session_security();
    ESP_LOGI(TAG, "password reset updated Aegis keys");
    request->send(200, "application/json", "{\"status\":\"ok\"}");
    show_password_reset_reboot_dialog();
    #endif
}

bool erase_nvs_namespace(const char* name, String& error)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(name, NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        return true;
    }
    if (err != ESP_OK)
    {
        error = "NVS open failed for ";
        error += name ? name : "namespace";
        error += ": ";
        error += esp_err_to_name(err);
        return false;
    }

    err = nvs_erase_all(handle);
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK)
    {
        error = "NVS erase failed for ";
        error += name ? name : "namespace";
        error += ": ";
        error += esp_err_to_name(err);
        return false;
    }
    return true;
}

void reset_memory(AsyncWebServerRequest* request)
{
    if (!request)
    {
        return;
    }

    #if BUILD_WITH_SECURITY_LEVEL > 0
    note_web_error();
    request->send(403, "text/plain", "Memory reset is only available under security level 0");
    #else
    String error;
    if (!bt_host_list)
    {
        note_web_error();
        request->send(500, "text/plain", "Bluetooth host list is unavailable");
        return;
    }
    if (!wifi_manager)
    {
        note_web_error();
        request->send(500, "text/plain", "Wi-Fi config is unavailable");
        return;
    }

    if (BtManager::shutdown() != BtManager::Result::Ok)
    {
        note_web_error();
        request->send(500, "text/plain", "Bluetooth shutdown failed");
        return;
    }

    if (!erase_nvs_namespace(kBluedroidNvsNamespace, error))
    {
        note_web_error();
        request->send(500, "text/plain", error);
        return;
    }

    if (!erase_nvs_namespace(kBtHostListNvsNamespace, error))
    {
        note_web_error();
        request->send(500, "text/plain", error);
        return;
    }
    bt_host_list->clear();

    if (!erase_nvs_namespace(kNetworkNvsNamespace, error))
    {
        note_web_error();
        request->send(500, "text/plain", error);
        return;
    }
    wifi_manager->clear();

    note_web_save();
    ESP_LOGI(TAG, "memory reset erased Bluetooth bonds, Bluetooth host list, and Wi-Fi/cloud config");
    request->send(200, "application/json", "{\"status\":\"ok\"}");
    show_memory_reset_reboot_dialog();
    #endif
}

} // namespace

const char* WebServer::sessionAuthResultName(SessionAuthResult result)
{
    switch (result)
    {
    case SessionAuthResult::Ok:
        return "Ok";
    case SessionAuthResult::SessionUnavailable:
        return "Session unavailable";
    case SessionAuthResult::MissingClientResponse:
        return "Missing session response";
    case SessionAuthResult::BadClientResponse:
        return "Bad session response";
    case SessionAuthResult::MissingClientSalt:
        return "Missing session salt";
    case SessionAuthResult::BadClientSalt:
        return "Bad session salt";
    case SessionAuthResult::NetworkKeyUnavailable:
        return "Network key unavailable";
    case SessionAuthResult::SessionKeyFailed:
        return "Session key failed";
    default:
        return "Unknown session authentication error";
    }
}

String WebServer::jsonString(const char* text)
{
    String encoded("\"");
    if (!text)
    {
        encoded += "\"";
        return encoded;
    }

    for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(text); *cursor; ++cursor)
    {
        switch (*cursor)
        {
        case '"':
            encoded += "\\\"";
            break;
        case '\\':
            encoded += "\\\\";
            break;
        case '\b':
            encoded += "\\b";
            break;
        case '\f':
            encoded += "\\f";
            break;
        case '\n':
            encoded += "\\n";
            break;
        case '\r':
            encoded += "\\r";
            break;
        case '\t':
            encoded += "\\t";
            break;
        default:
            if (*cursor < 0x20)
            {
                char escaped[7];
                snprintf(escaped, sizeof(escaped), "\\u%04X", static_cast<unsigned>(*cursor));
                encoded += escaped;
            }
            else
            {
                encoded += static_cast<char>(*cursor);
            }
            break;
        }
    }

    encoded += "\"";
    return encoded;
}

void WebServer::formatMac(const uint8_t* bdaddr, char* buffer, size_t buffer_size)
{
    if (!bdaddr || !buffer || buffer_size == 0)
    {
        return;
    }

    snprintf(buffer,
             buffer_size,
             "%02X:%02X:%02X:%02X:%02X:%02X",
             bdaddr[0],
             bdaddr[1],
             bdaddr[2],
             bdaddr[3],
             bdaddr[4],
             bdaddr[5]);
}

const AsyncWebParameter* WebServer::findRequestParam(AsyncWebServerRequest* request, const char* name)
{
    if (!request || !name)
    {
        return nullptr;
    }

    const AsyncWebParameter* param = request->getParam(name, false, false);
    if (param)
    {
        return param;
    }

    return request->getParam(name, true, false);
}

WebServer::SessionAuthResult WebServer::authenticateSessionRequest(AsyncWebServerRequest* request, uint8_t out_session_key[kSessionKeySize])
{
    if (!out_session_key)
    {
        return SessionAuthResult::SessionKeyFailed;
    }
    mbedtls_platform_zeroize(out_session_key, kSessionKeySize);

    if (!request || !g_session_security.challenge_valid || !g_session_security.response_valid)
    {
        return SessionAuthResult::SessionUnavailable;
    }

    const String& response_hex = request->header(kHeaderSessionResponseFromClient);
    if (response_hex.isEmpty())
    {
        return SessionAuthResult::MissingClientResponse;
    }
    const String& client_salt_hex = request->header(kHeaderSessionSaltFromClient);
    if (client_salt_hex.isEmpty())
    {
        return SessionAuthResult::MissingClientSalt;
    }

    uint8_t supplied_response[kSessionResponseSize] = {};
    if (!hex_to_bytes(response_hex, supplied_response, sizeof(supplied_response)))
    {
        return SessionAuthResult::BadClientResponse;
    }
    uint8_t supplied_client_salt[kSessionSaltHalfSize] = {};
    if (!hex_to_bytes(client_salt_hex, supplied_client_salt, sizeof(supplied_client_salt)))
    {
        mbedtls_platform_zeroize(supplied_response, sizeof(supplied_response));
        return SessionAuthResult::BadClientSalt;
    }

    const uint8_t* network_key = nullptr;
    if (!load_network_key(network_key))
    {
        mbedtls_platform_zeroize(supplied_response, sizeof(supplied_response));
        mbedtls_platform_zeroize(supplied_client_salt, sizeof(supplied_client_salt));
        return SessionAuthResult::NetworkKeyUnavailable;
    }

    uint8_t expected_response[kSessionResponseSize] = {};
    if (!compute_hmac(network_key, g_session_security.session_challenge, sizeof(g_session_security.session_challenge), expected_response) ||
        !constant_time_equal(supplied_response, expected_response, sizeof(expected_response)))
    {
        mbedtls_platform_zeroize(supplied_response, sizeof(supplied_response));
        mbedtls_platform_zeroize(supplied_client_salt, sizeof(supplied_client_salt));
        mbedtls_platform_zeroize(expected_response, sizeof(expected_response));
        return SessionAuthResult::BadClientResponse;
    }

    memcpy(g_session_security.session_salt_from_client, supplied_client_salt, sizeof(g_session_security.session_salt_from_client));
    uint8_t session_salt[kSessionSaltSize] = {};
    memcpy(session_salt, g_session_security.session_salt_from_server, sizeof(g_session_security.session_salt_from_server));
    memcpy(session_salt + sizeof(g_session_security.session_salt_from_server), supplied_client_salt, sizeof(supplied_client_salt));

    zero_session_key();
    const bool key_ok = Aegis::pbkdf2HmacSha256(network_key,
                                                Aegis::kNetworkKeySize,
                                                session_salt,
                                                sizeof(session_salt),
                                                Aegis::kPbkdfIterations,
                                                g_session_security.session_key,
                                                sizeof(g_session_security.session_key));
    mbedtls_platform_zeroize(supplied_response, sizeof(supplied_response));
    mbedtls_platform_zeroize(supplied_client_salt, sizeof(supplied_client_salt));
    mbedtls_platform_zeroize(expected_response, sizeof(expected_response));
    mbedtls_platform_zeroize(session_salt, sizeof(session_salt));

    if (!key_ok)
    {
        return SessionAuthResult::SessionKeyFailed;
    }

    g_session_security.session_key_valid = true;
    g_session_security.nonce_counter = 0;
    memcpy(out_session_key, g_session_security.session_key, kSessionKeySize);
    return SessionAuthResult::Ok;
}

uint64_t WebServer::nextSessionNonceCounter()
{
    return g_session_security.nonce_counter++;
}

void WebServer::fillSessionNonce(uint64_t counter, uint8_t nonce[kSessionGcmNonceSize])
{
    if (!nonce)
    {
        return;
    }

    memset(nonce, 0, kSessionGcmNonceSize);
    for (size_t i = 0; i < sizeof(counter); ++i)
    {
        nonce[4 + i] = static_cast<uint8_t>((counter >> ((sizeof(counter) - 1 - i) * 8)) & 0xFF);
    }
}

bool WebServer::init()
{
    if (g_initialized)
    {
        return true;
    }

    for (size_t i = 0; i < WEB_ASSETS_CNT; ++i)
    {
        const String route = asset_route(web_assets_list[i]);
        g_server.on(AsyncURIMatcher::exact(route), HTTP_GET, send_asset);
        ESP_LOGI(TAG, "registered web asset GET %s", route.c_str());
    }

    if (find_asset_by_name("index.html"))
    {
        g_server.on(AsyncURIMatcher::exact("/"), HTTP_GET, send_asset);
        ESP_LOGI(TAG, "registered web asset GET /");
    }

    g_server.on(AsyncURIMatcher::exact("/get_info"), HTTP_GET, send_info);
    ESP_LOGI(TAG, "registered device info GET /get_info");

    g_server.on(AsyncURIMatcher::exact("/download_file"), HTTP_GET, WebFileHandlers::sendMicroSdFile);
    ESP_LOGI(TAG, "registered microSD download GET /download_file");

    g_server.on(AsyncURIMatcher::exact("/delete_file"), HTTP_GET, WebFileHandlers::deleteMicroSdFile);
    ESP_LOGI(TAG, "registered microSD delete GET /delete_file");

    g_server.on(AsyncURIMatcher::exact("/list_files.json"), HTTP_GET, WebFileHandlers::sendMicroSdFileList);
    ESP_LOGI(TAG, "registered microSD file list GET /list_files.json");

    g_server.on(AsyncURIMatcher::exact("/get_cfg"), HTTP_GET, WebCfgHandlers::sendCfg);
    ESP_LOGI(TAG, "registered session-encrypted config GET /get_cfg");

    g_server.on(AsyncURIMatcher::exact("/set_cfg"), HTTP_POST, WebCfgHandlers::finishSetCfg, nullptr, WebCfgHandlers::writeSetCfgBody);
    ESP_LOGI(TAG, "registered encrypted config POST /set_cfg");

    g_server.on(AsyncURIMatcher::exact("/time_sync"), HTTP_POST, WebCfgHandlers::timeSync);
    ESP_LOGI(TAG, "registered time sync POST /time_sync");

    g_server.on(AsyncURIMatcher::exact("/reset_password"), HTTP_POST, reset_password);
    ESP_LOGI(TAG, "registered password reset POST /reset_password");

    g_server.on(AsyncURIMatcher::exact("/reset_memory"), HTTP_POST, reset_memory);
    ESP_LOGI(TAG, "registered memory reset POST /reset_memory");

    // Uploads are not encrypted; keep this endpoint off untrusted networks until it is replaced by session encryption.
    g_server.on(AsyncURIMatcher::exact("/file_upload"),
                HTTP_POST,
                WebFileHandlers::finishFileUpload,
                WebFileHandlers::writeFileUploadPart,
                WebFileHandlers::writeFileUploadBody);
    ESP_LOGI(TAG, "registered microSD upload POST /file_upload");

#if defined(BUILD_FTP_SERVER) && BUILD_WITH_SECURITY_LEVEL <= 1
    if (!FtpServer::start(MicroSdCard::fs(), kFtpUser, kFtpPassword))
    {
        ESP_LOGE(TAG, "FTP server start failed");
        return false;
    }

    ESP_LOGI(TAG, "FTP server started");
#endif

    //DiskStats::refreshDiskSpace();
    start_mdns();

    g_server.begin();
    g_initialized = true;
    ESP_LOGI(TAG, "web server started");

    return true;
}
