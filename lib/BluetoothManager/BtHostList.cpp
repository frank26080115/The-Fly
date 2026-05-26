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
#include "nvs.h"
#include "utilfuncs.h"

namespace
{

constexpr const char* TAG              = "BtHostList";
constexpr const char* kDefaultPath     = "/bluetooth.json";
constexpr size_t      kMaxJsonFileSize = 16 * 1024;
constexpr uint32_t    kBtHostListMagic   = 0x54464254; // "TFBT"
constexpr uint32_t    kBtHostListVersion = 3;
constexpr const char* kBtHostNvsNamespace = "bt_hosts";
constexpr const char* kBtHostNvsBlobName  = "hosts";

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

void refresh_host_runtime(bt_host_item_t& item)
{
    item.name_custom[sizeof(item.name_custom) - 1] = '\0';
    item.name_reported[sizeof(item.name_reported) - 1] = '\0';
    item.bonded = host_slot_used(item) && BtManager::isBonded(item.bdaddr);
}

void init_bt_host_list(bt_host_list_t& hosts)
{
    memset(&hosts, 0, sizeof(hosts));
    hosts.magic   = kBtHostListMagic;
    hosts.version = kBtHostListVersion;
}

void clear_host_slot(bt_host_item_t& item)
{
    memset(&item, 0, sizeof(item));
}

void sanitize_bt_host_list(bt_host_list_t& hosts)
{
    hosts.magic   = kBtHostListMagic;
    hosts.version = kBtHostListVersion;
    const size_t old_count = hosts.count > kBtHostListMaxEntries ? kBtHostListMaxEntries : hosts.count;
    uint8_t      new_count = 0;

    for (size_t i = 0; i < old_count; ++i)
    {
        bt_host_item_t item = hosts.host[i];
        if (!host_slot_used(item))
        {
            continue;
        }

        refresh_host_runtime(item);
        if (item.icon >= ICON_LAST)
        {
            item.icon = ICON_UNKNOWN;
        }
        hosts.host[new_count++] = item;
    }

    for (size_t i = new_count; i < kBtHostListMaxEntries; ++i)
    {
        clear_host_slot(hosts.host[i]);
    }

    hosts.count = new_count;
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
        if (hosts.host[i].bonded && hosts.host[i].icon == icon)
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
    clear();
}

BtHostList::~BtHostList()
{
    m_destroyed = true;
    clear();
}

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
        ESP_LOGW(TAG, "Bluetooth host import is too large: %llu bytes", static_cast<unsigned long long>(file_size));
        m_last_load_result = LoadResult::Ok;
        return true;
    }

    char* buffer = static_cast<char*>(malloc(static_cast<size_t>(file_size) + 1));
    if (!buffer)
    {
        file.close();
        ESP_LOGW(TAG, "could not allocate Bluetooth host import buffer");
        m_last_load_result = LoadResult::Ok;
        return true;
    }

    const int bytes_read = file.read(buffer, static_cast<size_t>(file_size));
    file.close();
    if (bytes_read < 0 || static_cast<uint64_t>(bytes_read) != file_size)
    {
        free(buffer);
        ESP_LOGW(TAG, "could not read Bluetooth host import");
        m_last_load_result = LoadResult::Ok;
        return true;
    }
    buffer[file_size] = '\0';

    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, buffer);
    free(buffer);
    if (error)
    {
        ESP_LOGW(TAG, "could not parse Bluetooth host import: %s", error.c_str());
        m_last_load_result = LoadResult::Ok;
        return true;
    }

    JsonArray hosts_json = doc["hosts"].as<JsonArray>();
    if (hosts_json.isNull())
    {
        ESP_LOGW(TAG, "Bluetooth host import is missing hosts array");
        m_last_load_result = LoadResult::Ok;
        return true;
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
        item.icon      = icon ? IconLookup::fromString(icon) : ICON_UNKNOWN;
        item.last_used = current_host_time();
        refresh_host_runtime(item);
        ++imported.count;
    }

    if (skipped > 0)
    {
        ESP_LOGW(TAG, "Bluetooth host import skipped %u invalid or extra item(s)", static_cast<unsigned>(skipped));
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
    esp_err_t err = nvs_open(kBtHostNvsNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        m_last_load_result = LoadResult::Ok;
        ESP_LOGI(TAG, "no Bluetooth host list namespace in NVS; using empty list");
        return true;
    }
    if (err != ESP_OK)
    {
        m_last_load_result = LoadResult::FileOpenFailed;
        ESP_LOGW(TAG, "could not open Bluetooth host NVS namespace: %s", esp_err_to_name(err));
        return false;
    }

    size_t hosts_size = 0;
    err = nvs_get_blob(handle, kBtHostNvsBlobName, nullptr, &hosts_size);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        nvs_close(handle);
        m_last_load_result = LoadResult::Ok;
        ESP_LOGI(TAG, "no Bluetooth host list in NVS; using empty list");
        return true;
    }
    if (err != ESP_OK)
    {
        nvs_close(handle);
        m_last_load_result = LoadResult::FileReadFailed;
        ESP_LOGW(TAG, "could not load Bluetooth host list from NVS: %s size=%u", esp_err_to_name(err), static_cast<unsigned>(hosts_size));
        return false;
    }

    if (hosts_size != sizeof(bt_host_list_t))
    {
        nvs_close(handle);
        m_last_load_result = LoadResult::Ok;
        ESP_LOGW(TAG, "could not load Bluetooth host list from NVS: unexpected size=%u", static_cast<unsigned>(hosts_size));
        return true;
    }

    bt_host_list_t hosts = {};
    size_t read_size = sizeof(hosts);
    err = nvs_get_blob(handle, kBtHostNvsBlobName, &hosts, &read_size);
    nvs_close(handle);
    if (err != ESP_OK || read_size != sizeof(hosts))
    {
        m_last_load_result = LoadResult::FileReadFailed;
        ESP_LOGW(TAG, "could not load Bluetooth host list from NVS: %s size=%u", esp_err_to_name(err), static_cast<unsigned>(read_size));
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
    m_hosts.count = static_cast<uint8_t>(m_size > kBtHostListMaxEntries ? kBtHostListMaxEntries : m_size);
    sanitize_bt_host_list(m_hosts);
    m_size = m_hosts.count;

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

bool BtHostList::copyHostList(bt_host_list_t& out) const
{
    out = m_hosts;
    sanitize_bt_host_list(out);
    return true;
}

bool BtHostList::replaceHostList(const bt_host_list_t& hosts)
{
    if (m_destroyed)
    {
        m_last_load_result = LoadResult::Destroyed;
        ESP_LOGW(TAG, "not replacing Bluetooth host list after destructor");
        return false;
    }

    bt_host_list_t staged = hosts;
    sanitize_bt_host_list(staged);

    m_hosts = staged;
    m_size = m_hosts.count;
    return pruneBonds();
}

bool BtHostList::pruneBonds()
{
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
}

bool BtHostList::insert(const char* name, const esp_bd_addr_t bdaddr, uint8_t icon)
{
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
        refresh_host_runtime(*existing);
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
    item.icon      = icon > ICON_UNKNOWN && icon < ICON_LAST ? icon : ICON_UNKNOWN;
    refresh_host_runtime(item);
    return true;
}

bool BtHostList::unpair(size_t index)
{
    m_last_load_result = LoadResult::Ok;

    if (m_destroyed)
    {
        m_last_load_result = LoadResult::Destroyed;
        ESP_LOGW(TAG, "not unpairing Bluetooth host after destructor");
        return false;
    }
    if (index >= m_size)
    {
        m_last_load_result = LoadResult::InvalidHost;
        ESP_LOGW(TAG, "could not unpair Bluetooth host at index %u", static_cast<unsigned>(index));
        return false;
    }

    bt_host_item_t& item = m_hosts.host[index];
    char mac[18];
    format_mac(item.bdaddr, mac, sizeof(mac));

    if (item.bonded)
    {
        const esp_err_t remove_err = esp_bt_gap_remove_bond_device(item.bdaddr);
        if (remove_err != ESP_OK)
        {
            ESP_LOGW(TAG, "could not unpair Bluetooth bond %s: %s", mac, esp_err_to_name(remove_err));
            return false;
        }
    }

    item.bonded = false;
    ESP_LOGI(TAG, "unpaired Bluetooth host %s while keeping list entry", mac);
    return true;
}

bool BtHostList::remove(size_t index, bool removeBond)
{
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
}

void BtHostList::clear()
{
    init_bt_host_list(m_hosts);
    m_size = 0;
}

size_t BtHostList::size() const
{
    return m_size;
}

bt_host_item_t* BtHostList::get(size_t index)
{
    return const_cast<bt_host_item_t*>(static_cast<const BtHostList*>(this)->get(index));
}

const bt_host_item_t* BtHostList::get(size_t index) const
{
    return index < m_size ? &m_hosts.host[index] : nullptr;
}

bt_host_item_t* BtHostList::getFirstPhone()
{
    return const_cast<bt_host_item_t*>(static_cast<const BtHostList*>(this)->getFirstPhone());
}

const bt_host_item_t* BtHostList::getFirstPhone() const
{
    return find_host_by_icon(m_hosts, ICON_SMARTPHONE);
}

bt_host_item_t* BtHostList::getFirstLaptop()
{
    return const_cast<bt_host_item_t*>(static_cast<const BtHostList*>(this)->getFirstLaptop());
}

const bt_host_item_t* BtHostList::getFirstLaptop() const
{
    return find_host_by_icon(m_hosts, ICON_LAPTOP);
}

BtHostList::LoadResult BtHostList::lastLoadResult() const
{
    return m_last_load_result;
}

const char* BtHostList::lastLoadResultName() const
{
    return load_result_tostring(m_last_load_result);
}
