#include "BtHostList.h"

#include <ArduinoJson.h>
#include <Arduino.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "BluetoothManager.h"
#include "IconLookup.h"
#include "MicroSdCard.h"
#include "esp_err.h"
#include "esp_log.h"
#ifdef BUILD_WITH_SECURITY
#include "nvs.h"
#endif
#include "utilfuncs.h"

namespace
{

constexpr const char* TAG              = "BtHostList";
constexpr const char* kDefaultPath     = "/bluetooth.json";
constexpr size_t      kMaxJsonFileSize = 16 * 1024;
#ifdef BUILD_WITH_SECURITY
constexpr uint32_t    kBtHostListMagic   = 0x54464254; // "TFBT"
constexpr uint32_t    kBtHostListVersion = 1;
constexpr const char* kBtHostNvsNamespace = "bt_hosts";
constexpr const char* kBtHostNvsBlobName  = "hosts";
#endif

const char* load_result_tostring(BtHostList::LoadResult result)
{
    switch (result)
    {
    case BtHostList::LoadResult::Ok:
        return "Ok";
    case BtHostList::LoadResult::SdNotReady:
        return "SdNotReady";
    case BtHostList::LoadResult::FileOpenFailed:
        return "FileOpenFailed";
    case BtHostList::LoadResult::FileTooLarge:
        return "FileTooLarge";
    case BtHostList::LoadResult::FileReadFailed:
        return "FileReadFailed";
    case BtHostList::LoadResult::JsonParseFailed:
        return "JsonParseFailed";
    case BtHostList::LoadResult::MissingHosts:
        return "MissingHosts";
    case BtHostList::LoadResult::InvalidHost:
        return "InvalidHost";
    case BtHostList::LoadResult::AllocationFailed:
        return "AllocationFailed";
    case BtHostList::LoadResult::EmptyList:
        return "EmptyList";
    case BtHostList::LoadResult::Destroyed:
        return "Destroyed";
    case BtHostList::LoadResult::FileWriteFailed:
        return "FileWriteFailed";
    default:
        return "Unknown";
    }
}

#ifndef BUILD_WITH_SECURITY
bt_host_item_t* create_host(const char* name, const esp_bd_addr_t bdaddr, uint8_t icon)
{
    bt_host_item_t* item = static_cast<bt_host_item_t*>(calloc(1, sizeof(bt_host_item_t)));
    if (!item)
    {
        return nullptr;
    }

    copy_bda(item->bdaddr, bdaddr);
    item->name = clone_string(name);
    if (!item->name)
    {
        free(item);
        return nullptr;
    }

    item->bonded    = BtManager::isBonded(item->bdaddr);
    item->icon      = icon;
    item->next_node = nullptr;
    return item;
}

bt_host_item_t* find_host(bt_host_item_t* head, const esp_bd_addr_t bdaddr)
{
    bt_host_item_t* item = head;
    while (item)
    {
        if (bda_equal(item->bdaddr, bdaddr))
        {
            return item;
        }
        item = static_cast<bt_host_item_t*>(item->next_node);
    }
    return nullptr;
}

const bt_host_item_t* find_host_by_icon(const bt_host_item_t* head, uint8_t icon)
{
    const bt_host_item_t* item = head;
    while (item)
    {
        if (item->icon == icon)
        {
            return item;
        }
        item = static_cast<bt_host_item_t*>(item->next_node);
    }
    return nullptr;
}
#else
bool host_slot_used(const bt_host_item_t& item)
{
    const esp_bd_addr_t empty = {};
    return !bda_equal(item.bdaddr, empty);
}

time_t current_host_time()
{
    time_t now = time(nullptr);
    if (now <= 0)
    {
        now = static_cast<time_t>(millis() / 1000);
    }
    return now;
}

void refresh_host_display_name(bt_host_item_t& item)
{
    const char* display = item.name_custom[0] != '\0' ? item.name_custom : item.name_reported;
    strlcpy(item.name, display ? display : "", sizeof(item.name));
    item.next_node = nullptr;
}

void refresh_host_runtime(bt_host_item_t& item)
{
    item.name[sizeof(item.name) - 1] = '\0';
    item.name_custom[sizeof(item.name_custom) - 1] = '\0';
    item.name_reported[sizeof(item.name_reported) - 1] = '\0';
    item.bonded = host_slot_used(item) && BtManager::isBonded(item.bdaddr);
    refresh_host_display_name(item);
}

void init_bt_host_list(bt_host_list_t& hosts)
{
    memset(&hosts, 0, sizeof(hosts));
    hosts.magic   = kBtHostListMagic;
    hosts.version = kBtHostListVersion;
}

void sanitize_bt_host_list(bt_host_list_t& hosts)
{
    hosts.magic   = kBtHostListMagic;
    hosts.version = kBtHostListVersion;
    hosts.count   = hosts.count > kBtHostListMaxEntries ? kBtHostListMaxEntries : hosts.count;
    for (size_t i = 0; i < kBtHostListMaxEntries; ++i)
    {
        refresh_host_runtime(hosts.host[i]);
    }
}

bt_host_item_t* find_host(bt_host_list_t& hosts, const esp_bd_addr_t bdaddr)
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

const bt_host_item_t* find_host_by_icon(const bt_host_list_t& hosts, uint8_t icon)
{
    for (size_t i = 0; i < hosts.count; ++i)
    {
        if (hosts.host[i].icon == icon)
        {
            return &hosts.host[i];
        }
    }
    return nullptr;
}

size_t choose_overwrite_slot(bt_host_list_t& hosts)
{
    size_t selected = 0;
    bool found_unbonded = false;
    time_t oldest = 0;

    for (size_t i = 0; i < hosts.count; ++i)
    {
        refresh_host_runtime(hosts.host[i]);
        if (hosts.host[i].bonded)
        {
            continue;
        }
        if (!found_unbonded || hosts.host[i].last_used < oldest)
        {
            selected = i;
            oldest = hosts.host[i].last_used;
            found_unbonded = true;
        }
    }

    if (found_unbonded)
    {
        return selected;
    }

    oldest = hosts.host[0].last_used;
    for (size_t i = 1; i < hosts.count; ++i)
    {
        if (hosts.host[i].last_used < oldest)
        {
            selected = i;
            oldest = hosts.host[i].last_used;
        }
    }
    return selected;
}
#endif

void format_mac(const esp_bd_addr_t bdaddr, char* buffer, size_t buffer_size)
{
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

} // namespace

BtHostList::BtHostList()
{
    #ifndef BUILD_WITH_SECURITY
    strlcpy(m_path, kDefaultPath, sizeof(m_path));
    #else
    clear();
    #endif
}

BtHostList::~BtHostList()
{
    m_destroyed = true;
    clear();
}

#ifndef BUILD_WITH_SECURITY
bool BtHostList::loadFromMicroSd(const char* path)
{
    clear();
    m_last_load_result = LoadResult::Ok;
    strlcpy(m_path, path ? path : kDefaultPath, sizeof(m_path));

    if (!MicroSdCard::isReady())
    {
        m_last_load_result = LoadResult::SdNotReady;
        ESP_LOGW(TAG, "microSD is not ready while loading bluetooth hosts");
        return false;
    }

    FsFile file;
    if (!file.open(m_path, O_RDONLY))
    {
        m_last_load_result = LoadResult::FileOpenFailed;
        ESP_LOGW(TAG, "could not open bluetooth host list: %s", m_path);
        return false;
    }

    const uint64_t file_size = file.fileSize();
    if (file_size > kMaxJsonFileSize)
    {
        file.close();
        m_last_load_result = LoadResult::FileTooLarge;
        ESP_LOGW(TAG, "bluetooth host list is too large: %llu bytes", static_cast<unsigned long long>(file_size));
        return false;
    }

    char* buffer = static_cast<char*>(malloc(static_cast<size_t>(file_size) + 1));
    if (!buffer)
    {
        file.close();
        m_last_load_result = LoadResult::AllocationFailed;
        ESP_LOGW(TAG, "could not allocate bluetooth host list buffer");
        return false;
    }

    const int bytes_read = file.read(buffer, static_cast<size_t>(file_size));
    file.close();

    if (bytes_read < 0 || static_cast<uint64_t>(bytes_read) != file_size)
    {
        free(buffer);
        m_last_load_result = LoadResult::FileReadFailed;
        ESP_LOGW(TAG, "could not read bluetooth host list");
        return false;
    }
    buffer[file_size] = '\0';

    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, buffer);
    free(buffer);

    if (error)
    {
        m_last_load_result = LoadResult::JsonParseFailed;
        ESP_LOGW(TAG, "could not parse bluetooth host list: %s", error.c_str());
        return false;
    }

    JsonArray hosts = doc["hosts"].as<JsonArray>();
    if (hosts.isNull())
    {
        m_last_load_result = LoadResult::MissingHosts;
        ESP_LOGW(TAG, "bluetooth host list is missing hosts array");
        return false;
    }

    size_t skipped = 0;
    for (JsonVariant host_value : hosts)
    {
        JsonObject host = host_value.as<JsonObject>();
        if (host.isNull())
        {
            ++skipped;
            continue;
        }

        const char* name = host["name"].is<const char*>() ? host["name"].as<const char*>() : "";
        const char* mac  = host["mac"].as<const char*>();
        const char* icon = host["icon"].as<const char*>();

        esp_bd_addr_t bdaddr = {};
        if (!parse_mac(mac, bdaddr))
        {
            ++skipped;
            continue;
        }

        bt_host_item_t* item = create_host(name, bdaddr, icon ? IconLookup::fromString(icon) : ICON_BLUETOOTH);
        if (!item)
        {
            clear();
            m_last_load_result = LoadResult::AllocationFailed;
            ESP_LOGW(TAG, "could not allocate bluetooth host item");
            return false;
        }

        if (m_tail)
        {
            m_tail->next_node = item;
        }
        else
        {
            m_head = item;
        }

        m_tail = item;
        ++m_size;
    }

    if (skipped > 0)
    {
        m_last_load_result = LoadResult::InvalidHost;
        ESP_LOGW(TAG, "loaded %u bluetooth hosts, skipped %u invalid entries", static_cast<unsigned>(m_size), static_cast<unsigned>(skipped));
        return false;
    }

    ESP_LOGI(TAG, "loaded %u bluetooth hosts", static_cast<unsigned>(m_size));
    return true;
}

bool BtHostList::saveToMicroSd(bool allowEmpty)
{
    m_last_load_result = LoadResult::Ok;

    if (m_destroyed)
    {
        m_last_load_result = LoadResult::Destroyed;
        ESP_LOGW(TAG, "not saving bluetooth host list after destructor");
        return false;
    }

    if (m_size == 0 && !allowEmpty)
    {
        m_last_load_result = LoadResult::EmptyList;
        ESP_LOGW(TAG, "not saving empty bluetooth host list");
        return false;
    }

    JsonDocument doc;
    JsonArray    hosts = doc["hosts"].to<JsonArray>();

    for (bt_host_item_t* item = m_head; item; item = static_cast<bt_host_item_t*>(item->next_node))
    {
        JsonObject host = hosts.add<JsonObject>();
        char       mac[18];
        format_mac(item->bdaddr, mac, sizeof(mac));

        host["name"] = item->name ? item->name : "";
        host["mac"]  = String(mac);
        if (item->icon > ICON_UNKNOWN && item->icon < ICON_LAST && item->icon != ICON_BLUETOOTH)
        {
            host["icon"] = IconLookup::toString(item->icon);
        }
    }

    FsFile file;
    if (!file.open(m_path, O_WRONLY | O_CREAT | O_TRUNC))
    {
        m_last_load_result = LoadResult::FileOpenFailed;
        ESP_LOGW(TAG, "could not open bluetooth host list for write: %s", m_path);
        return false;
    }

    const size_t bytes_written = serializeJsonPretty(doc, file);
    file.println();
    const bool synced = file.sync();
    file.close();

    if (bytes_written == 0 || !synced)
    {
        m_last_load_result = LoadResult::FileWriteFailed;
        ESP_LOGW(TAG, "could not write bluetooth host list: %s", m_path);
        return false;
    }

    ESP_LOGI(TAG, "saved %u bluetooth hosts to %s", static_cast<unsigned>(m_size), m_path);
    return true;
}
#else
bool BtHostList::loadFromMicroSd(const char* path)
{
    if (!loadFromNvs())
    {
        return false;
    }

    const char* import_path = path ? path : kDefaultPath;
    if (!MicroSdCard::isReady())
    {
        ESP_LOGI(TAG, "microSD is not ready; keeping Bluetooth host list from NVS");
        return true;
    }

    FsFile file;
    if (!file.open(import_path, O_RDONLY))
    {
        ESP_LOGI(TAG, "no Bluetooth JSON import found at %s; keeping Bluetooth host list from NVS", import_path);
        return true;
    }

    const uint64_t file_size = file.fileSize();
    if (file_size > kMaxJsonFileSize)
    {
        file.close();
        m_last_load_result = LoadResult::FileTooLarge;
        ESP_LOGW(TAG, "Bluetooth host import is too large: %llu bytes", static_cast<unsigned long long>(file_size));
        return false;
    }

    char* buffer = static_cast<char*>(malloc(static_cast<size_t>(file_size) + 1));
    if (!buffer)
    {
        file.close();
        m_last_load_result = LoadResult::AllocationFailed;
        ESP_LOGW(TAG, "could not allocate Bluetooth host import buffer");
        return false;
    }

    const int bytes_read = file.read(buffer, static_cast<size_t>(file_size));
    file.close();
    if (bytes_read < 0 || static_cast<uint64_t>(bytes_read) != file_size)
    {
        free(buffer);
        m_last_load_result = LoadResult::FileReadFailed;
        ESP_LOGW(TAG, "could not read Bluetooth host import");
        return false;
    }
    buffer[file_size] = '\0';

    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, buffer);
    free(buffer);
    if (error)
    {
        m_last_load_result = LoadResult::JsonParseFailed;
        ESP_LOGW(TAG, "could not parse Bluetooth host import: %s", error.c_str());
        return false;
    }

    JsonArray hosts_json = doc["hosts"].as<JsonArray>();
    if (hosts_json.isNull())
    {
        m_last_load_result = LoadResult::MissingHosts;
        ESP_LOGW(TAG, "Bluetooth host import is missing hosts array");
        return false;
    }

    bt_host_list_t imported = {};
    init_bt_host_list(imported);
    size_t skipped = 0;
    for (JsonVariant host_value : hosts_json)
    {
        if (imported.count >= kBtHostListMaxEntries)
        {
            ++skipped;
            continue;
        }

        JsonObject host = host_value.as<JsonObject>();
        const char* name = host["name"].is<const char*>() ? host["name"].as<const char*>() : "";
        const char* mac  = host["mac"].as<const char*>();
        const char* icon = host["icon"].as<const char*>();

        esp_bd_addr_t bdaddr = {};
        if (host.isNull() || !parse_mac(mac, bdaddr))
        {
            ++skipped;
            continue;
        }

        bt_host_item_t& item = imported.host[imported.count];
        copy_bda(item.bdaddr, bdaddr);
        if (strlcpy(item.name_custom, name ? name : "", sizeof(item.name_custom)) >= sizeof(item.name_custom) ||
            strlcpy(item.name_reported, name ? name : "", sizeof(item.name_reported)) >= sizeof(item.name_reported))
        {
            ++skipped;
            continue;
        }
        item.icon      = icon ? IconLookup::fromString(icon) : ICON_BLUETOOTH;
        item.last_used = current_host_time();
        refresh_host_runtime(item);
        ++imported.count;
    }

    if (skipped > 0)
    {
        m_last_load_result = LoadResult::InvalidHost;
        ESP_LOGW(TAG, "Bluetooth host import has %u invalid item(s)", static_cast<unsigned>(skipped));
        return false;
    }

    sanitize_bt_host_list(imported);
    m_hosts = imported;
    m_size = m_hosts.count;
    if (!saveToNvs())
    {
        return false;
    }

    m_last_load_result = LoadResult::Ok;
    ESP_LOGI(TAG, "imported %u Bluetooth hosts into NVS", static_cast<unsigned>(m_size));
    return true;
}

bool BtHostList::saveToMicroSd(bool allowEmpty)
{
    if (m_destroyed)
    {
        m_last_load_result = LoadResult::Destroyed;
        ESP_LOGW(TAG, "not saving Bluetooth host list after destructor");
        return false;
    }

    if (m_size == 0 && !allowEmpty)
    {
        m_last_load_result = LoadResult::EmptyList;
        ESP_LOGW(TAG, "not saving empty Bluetooth host list");
        return false;
    }

    return saveToNvs();
}

bool BtHostList::loadFromNvs()
{
    clear();

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kBtHostNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        m_last_load_result = LoadResult::FileOpenFailed;
        ESP_LOGW(TAG, "could not open Bluetooth host NVS namespace: %s", esp_err_to_name(err));
        return false;
    }

    bt_host_list_t hosts = {};
    size_t hosts_size = sizeof(hosts);
    err = nvs_get_blob(handle, kBtHostNvsBlobName, &hosts, &hosts_size);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        m_last_load_result = LoadResult::Ok;
        ESP_LOGI(TAG, "no Bluetooth host list in NVS; using empty list");
        return true;
    }
    if (err != ESP_OK || hosts_size != sizeof(hosts))
    {
        m_last_load_result = LoadResult::FileReadFailed;
        ESP_LOGW(TAG, "could not load Bluetooth host list from NVS: %s size=%u", esp_err_to_name(err), static_cast<unsigned>(hosts_size));
        return false;
    }
    if (hosts.magic != kBtHostListMagic || hosts.version != kBtHostListVersion)
    {
        m_last_load_result = LoadResult::Ok;
        ESP_LOGW(TAG, "ignoring incompatible Bluetooth host list in NVS");
        return true;
    }

    sanitize_bt_host_list(hosts);
    m_hosts = hosts;
    m_size = m_hosts.count;
    m_last_load_result = LoadResult::Ok;
    ESP_LOGI(TAG, "loaded %u Bluetooth hosts from NVS", static_cast<unsigned>(m_size));
    return true;
}

bool BtHostList::saveToNvs()
{
    sanitize_bt_host_list(m_hosts);
    m_hosts.count = static_cast<uint8_t>(m_size);

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kBtHostNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        m_last_load_result = LoadResult::FileOpenFailed;
        ESP_LOGW(TAG, "could not open Bluetooth host NVS namespace for write: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(handle, kBtHostNvsBlobName, &m_hosts, sizeof(m_hosts));
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK)
    {
        m_last_load_result = LoadResult::FileWriteFailed;
        ESP_LOGW(TAG, "could not save Bluetooth host list to NVS: %s", esp_err_to_name(err));
        return false;
    }

    m_last_load_result = LoadResult::Ok;
    return true;
}
#endif

bool BtHostList::pruneBonds()
{
    #ifndef BUILD_WITH_SECURITY
    const int bonded_count = esp_bt_gap_get_bond_device_num();
    if (bonded_count == ESP_ERR_INVALID_STATE)
    {
        ESP_LOGW(TAG, "could not prune bluetooth bonds, gap is not initialized");
        return false;
    }
    if (bonded_count <= 0)
    {
        ESP_LOGI(TAG, "no bluetooth bonds to prune");
        return true;
    }

    esp_bd_addr_t* bonded = static_cast<esp_bd_addr_t*>(calloc(bonded_count, sizeof(esp_bd_addr_t)));
    if (!bonded)
    {
        ESP_LOGW(TAG, "could not allocate bluetooth bond list");
        return false;
    }

    int listed = bonded_count;
    const esp_err_t list_err = esp_bt_gap_get_bond_device_list(&listed, bonded);
    if (list_err != ESP_OK)
    {
        ESP_LOGW(TAG, "could not get bluetooth bond list: %s", esp_err_to_name(list_err));
        free(bonded);
        return false;
    }

    bool ok = true;
    int  pruned = 0;
    for (int i = 0; i < listed; ++i)
    {
        bt_host_item_t* host = find_host(m_head, bonded[i]);
        if (host)
        {
            host->bonded = true;
            continue;
        }

        char mac[18];
        format_mac(bonded[i], mac, sizeof(mac));
        const esp_err_t remove_err = esp_bt_gap_remove_bond_device(bonded[i]);
        if (remove_err != ESP_OK)
        {
            ESP_LOGW(TAG, "could not prune bluetooth bond %s: %s", mac, esp_err_to_name(remove_err));
            ok = false;
            continue;
        }

        ESP_LOGI(TAG, "pruned bluetooth bond %s", mac);
        ++pruned;
    }

    free(bonded);

    ESP_LOGI(TAG, "pruned %d bluetooth bonds", pruned);
    return ok;
    #else // BUILD_WITH_SECURITY
    const int bonded_count = esp_bt_gap_get_bond_device_num();
    if (bonded_count == ESP_ERR_INVALID_STATE)
    {
        ESP_LOGW(TAG, "could not prune bluetooth bonds, gap is not initialized");
        return false;
    }

    for (size_t i = 0; i < m_size; ++i)
    {
        refresh_host_runtime(m_hosts.host[i]);
    }

    if (bonded_count <= 0)
    {
        ESP_LOGI(TAG, "no bluetooth bonds to prune");
        return saveToNvs();
    }

    esp_bd_addr_t* bonded = static_cast<esp_bd_addr_t*>(calloc(bonded_count, sizeof(esp_bd_addr_t)));
    if (!bonded)
    {
        ESP_LOGW(TAG, "could not allocate bluetooth bond list");
        return false;
    }

    int listed = bonded_count;
    const esp_err_t list_err = esp_bt_gap_get_bond_device_list(&listed, bonded);
    if (list_err != ESP_OK)
    {
        ESP_LOGW(TAG, "could not get bluetooth bond list: %s", esp_err_to_name(list_err));
        free(bonded);
        return false;
    }

    bool ok = true;
    int  pruned = 0;
    for (int i = 0; i < listed; ++i)
    {
        bt_host_item_t* host = find_host(m_hosts, bonded[i]);
        if (host)
        {
            host->bonded = true;
            continue;
        }

        char mac[18];
        format_mac(bonded[i], mac, sizeof(mac));
        const esp_err_t remove_err = esp_bt_gap_remove_bond_device(bonded[i]);
        if (remove_err != ESP_OK)
        {
            ESP_LOGW(TAG, "could not prune bluetooth bond %s: %s", mac, esp_err_to_name(remove_err));
            ok = false;
            continue;
        }

        ESP_LOGI(TAG, "pruned bluetooth bond %s", mac);
        ++pruned;
    }

    free(bonded);

    if (!saveToNvs())
    {
        ok = false;
    }
    ESP_LOGI(TAG, "pruned %d bluetooth bonds", pruned);
    return ok;
    #endif
}

bool BtHostList::insert(const char* name, const esp_bd_addr_t bdaddr, uint8_t icon)
{
    #ifndef BUILD_WITH_SECURITY
    m_last_load_result = LoadResult::Ok;

    bt_host_item_t* existing = find_host(m_head, bdaddr);
    if (existing)
    {
        existing->bonded = true;

        if ((!existing->name || existing->name[0] == '\0') && name && name[0] != '\0')
        {
            char* replacement = clone_string(name);
            if (!replacement)
            {
                m_last_load_result = LoadResult::AllocationFailed;
                ESP_LOGW(TAG, "could not allocate bluetooth host name");
                return false;
            }

            free(existing->name);
            existing->name = replacement;
        }

        return true;
    }

    bt_host_item_t* item = create_host(name, bdaddr, icon);
    if (!item)
    {
        m_last_load_result = LoadResult::AllocationFailed;
        ESP_LOGW(TAG, "could not allocate bluetooth host item");
        return false;
    }

    item->bonded = true;

    if (m_tail)
    {
        m_tail->next_node = item;
    }
    else
    {
        m_head = item;
    }

    m_tail = item;
    ++m_size;
    return true;

    #else // BUILD_WITH_SECURITY
    m_last_load_result = LoadResult::Ok;

    if (m_destroyed)
    {
        m_last_load_result = LoadResult::Destroyed;
        ESP_LOGW(TAG, "not inserting Bluetooth host after destructor");
        return false;
    }
    if (name && strlen(name) >= kBtHostNameMaxLength)
    {
        m_last_load_result = LoadResult::InvalidHost;
        ESP_LOGW(TAG, "Bluetooth reported host name is too long");
        return false;
    }

    bt_host_item_t* existing = find_host(m_hosts, bdaddr);
    if (existing)
    {
        if (name && name[0] != '\0' && strlcpy(existing->name_reported, name, sizeof(existing->name_reported)) >= sizeof(existing->name_reported))
        {
            m_last_load_result = LoadResult::InvalidHost;
            ESP_LOGW(TAG, "Bluetooth reported host name is too long");
            return false;
        }
        existing->bonded = true;
        existing->last_used = current_host_time();
        if (icon > ICON_UNKNOWN && icon < ICON_LAST)
        {
            existing->icon = icon;
        }
        refresh_host_display_name(*existing);
        return true;
    }

    size_t slot = 0;
    if (m_size < kBtHostListMaxEntries)
    {
        slot = m_size;
        ++m_size;
        m_hosts.count = static_cast<uint8_t>(m_size);
    }
    else
    {
        slot = choose_overwrite_slot(m_hosts);
    }

    bt_host_item_t& item = m_hosts.host[slot];
    memset(&item, 0, sizeof(item));
    copy_bda(item.bdaddr, bdaddr);
    if (name && name[0] != '\0')
    {
        strlcpy(item.name_reported, name, sizeof(item.name_reported));
    }

    item.bonded    = true;
    item.last_used = current_host_time();
    item.icon      = icon > ICON_UNKNOWN && icon < ICON_LAST ? icon : ICON_BLUETOOTH;
    refresh_host_display_name(item);
    return true;
    #endif
}

bool BtHostList::remove(size_t index, bool removeBond)
{
    #ifndef BUILD_WITH_SECURITY
    m_last_load_result = LoadResult::Ok;

    if (m_destroyed)
    {
        m_last_load_result = LoadResult::Destroyed;
        ESP_LOGW(TAG, "not removing bluetooth host after destructor");
        return false;
    }

    bt_host_item_t* previous = nullptr;
    bt_host_item_t* item     = m_head;
    size_t          current  = 0;
    while (item && current < index)
    {
        previous = item;
        item     = static_cast<bt_host_item_t*>(item->next_node);
        ++current;
    }

    if (!item)
    {
        m_last_load_result = LoadResult::InvalidHost;
        ESP_LOGW(TAG, "could not remove bluetooth host at index %u", static_cast<unsigned>(index));
        return false;
    }

    char mac[18];
    format_mac(item->bdaddr, mac, sizeof(mac));

    if (removeBond && item->bonded)
    {
        const esp_err_t remove_err = esp_bt_gap_remove_bond_device(item->bdaddr);
        if (remove_err != ESP_OK)
        {
            ESP_LOGW(TAG, "could not remove bluetooth bond %s: %s", mac, esp_err_to_name(remove_err));
        }
    }

    bt_host_item_t* next = static_cast<bt_host_item_t*>(item->next_node);
    if (previous)
    {
        previous->next_node = next;
    }
    else
    {
        m_head = next;
    }

    if (m_tail == item)
    {
        m_tail = previous;
    }

    free(item->name);
    free(item);
    --m_size;

    ESP_LOGI(TAG, "removed bluetooth host %s", mac);
    return true;
    #else
    m_last_load_result = LoadResult::Ok;

    if (m_destroyed)
    {
        m_last_load_result = LoadResult::Destroyed;
        ESP_LOGW(TAG, "not removing bluetooth host after destructor");
        return false;
    }
    if (index >= m_size)
    {
        m_last_load_result = LoadResult::InvalidHost;
        ESP_LOGW(TAG, "could not remove bluetooth host at index %u", static_cast<unsigned>(index));
        return false;
    }

    char mac[18];
    format_mac(m_hosts.host[index].bdaddr, mac, sizeof(mac));

    if (removeBond && m_hosts.host[index].bonded)
    {
        const esp_err_t remove_err = esp_bt_gap_remove_bond_device(m_hosts.host[index].bdaddr);
        if (remove_err != ESP_OK)
        {
            ESP_LOGW(TAG, "could not remove bluetooth bond %s: %s", mac, esp_err_to_name(remove_err));
        }
    }

    for (size_t i = index; i + 1 < m_size; ++i)
    {
        m_hosts.host[i] = m_hosts.host[i + 1];
    }
    if (m_size > 0)
    {
        --m_size;
    }
    memset(&m_hosts.host[m_size], 0, sizeof(m_hosts.host[m_size]));
    m_hosts.count = static_cast<uint8_t>(m_size);

    ESP_LOGI(TAG, "removed bluetooth host %s", mac);
    return true;
    #endif
}

void BtHostList::clear()
{
    #ifndef BUILD_WITH_SECURITY
    bt_host_item_t* item = m_head;
    while (item)
    {
        bt_host_item_t* next = static_cast<bt_host_item_t*>(item->next_node);
        free(item->name);
        free(item);
        item = next;
    }

    m_head = nullptr;
    m_tail = nullptr;
    m_size = 0;
    #else // BUILD_WITH_SECURITY
    init_bt_host_list(m_hosts);
    m_size = 0;
    #endif
}

size_t BtHostList::size() const
{
    #ifndef BUILD_WITH_SECURITY
    return m_size;
    #else // BUILD_WITH_SECURITY
    return m_size;
    #endif
}

bt_host_item_t* BtHostList::get(size_t index)
{
    return const_cast<bt_host_item_t*>(static_cast<const BtHostList*>(this)->get(index));
}

const bt_host_item_t* BtHostList::get(size_t index) const
{
    #ifndef BUILD_WITH_SECURITY
    const bt_host_item_t* item = m_head;
    while (item && index > 0)
    {
        item = static_cast<const bt_host_item_t*>(item->next_node);
        --index;
    }
    return item;
    #else // BUILD_WITH_SECURITY
    return index < m_size ? &m_hosts.host[index] : nullptr;
    #endif
}

bt_host_item_t* BtHostList::getFirstPhone()
{
    #ifndef BUILD_WITH_SECURITY
    return const_cast<bt_host_item_t*>(find_host_by_icon(m_head, ICON_PHONE));
    #else
    return const_cast<bt_host_item_t*>(static_cast<const BtHostList*>(this)->getFirstPhone());
    #endif
}

const bt_host_item_t* BtHostList::getFirstPhone() const
{
    #ifndef BUILD_WITH_SECURITY
    return find_host_by_icon(m_head, ICON_PHONE);
    #else
    return find_host_by_icon(m_hosts, ICON_PHONE);
    #endif
}

bt_host_item_t* BtHostList::getFirstLaptop()
{
    #ifndef BUILD_WITH_SECURITY
    return const_cast<bt_host_item_t*>(find_host_by_icon(m_head, ICON_LAPTOP));
    #else
    return const_cast<bt_host_item_t*>(static_cast<const BtHostList*>(this)->getFirstLaptop());
    #endif
}

const bt_host_item_t* BtHostList::getFirstLaptop() const
{
    #ifndef BUILD_WITH_SECURITY
    return find_host_by_icon(m_head, ICON_LAPTOP);
    #else
    return find_host_by_icon(m_hosts, ICON_LAPTOP);
    #endif
}

BtHostList::LoadResult BtHostList::lastLoadResult() const
{
    return m_last_load_result;
}

const char* BtHostList::lastLoadResultName() const
{
    return load_result_tostring(m_last_load_result);
}
