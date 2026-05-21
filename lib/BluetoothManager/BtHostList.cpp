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
#include "esp_log.h"
#include "utilfuncs.h"

namespace
{

constexpr const char* TAG              = "BtHostList";
constexpr const char* kDefaultPath     = "/bluetooth.json";
constexpr size_t      kMaxJsonFileSize = 16 * 1024;

const char* result_name(BtHostList::LoadResult result)
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

char* clone_string(const char* value)
{
    const char* safe_value = value ? value : "";
    const size_t length    = strlen(safe_value);
    char*        clone     = static_cast<char*>(malloc(length + 1));
    if (!clone)
    {
        return nullptr;
    }
    memcpy(clone, safe_value, length + 1);
    return clone;
}

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
    strlcpy(m_path, kDefaultPath, sizeof(m_path));
}

BtHostList::~BtHostList()
{
    m_destroyed = true;
    clear();
}

bool BtHostList::loadFromMicroSd(const char* path)
{
    clear();
    m_last_result = LoadResult::Ok;
    strlcpy(m_path, path ? path : kDefaultPath, sizeof(m_path));

    if (!MicroSdCard::isReady())
    {
        m_last_result = LoadResult::SdNotReady;
        ESP_LOGW(TAG, "microSD is not ready while loading bluetooth hosts");
        return false;
    }

    FsFile file;
    if (!file.open(m_path, O_RDONLY))
    {
        m_last_result = LoadResult::FileOpenFailed;
        ESP_LOGW(TAG, "could not open bluetooth host list: %s", m_path);
        return false;
    }

    const uint64_t file_size = file.fileSize();
    if (file_size > kMaxJsonFileSize)
    {
        file.close();
        m_last_result = LoadResult::FileTooLarge;
        ESP_LOGW(TAG, "bluetooth host list is too large: %llu bytes", static_cast<unsigned long long>(file_size));
        return false;
    }

    char* buffer = static_cast<char*>(malloc(static_cast<size_t>(file_size) + 1));
    if (!buffer)
    {
        file.close();
        m_last_result = LoadResult::AllocationFailed;
        ESP_LOGW(TAG, "could not allocate bluetooth host list buffer");
        return false;
    }

    const int bytes_read = file.read(buffer, static_cast<size_t>(file_size));
    file.close();

    if (bytes_read < 0 || static_cast<uint64_t>(bytes_read) != file_size)
    {
        free(buffer);
        m_last_result = LoadResult::FileReadFailed;
        ESP_LOGW(TAG, "could not read bluetooth host list");
        return false;
    }
    buffer[file_size] = '\0';

    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, buffer);
    free(buffer);

    if (error)
    {
        m_last_result = LoadResult::JsonParseFailed;
        ESP_LOGW(TAG, "could not parse bluetooth host list: %s", error.c_str());
        return false;
    }

    JsonArray hosts = doc["hosts"].as<JsonArray>();
    if (hosts.isNull())
    {
        m_last_result = LoadResult::MissingHosts;
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
            m_last_result = LoadResult::AllocationFailed;
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
        m_last_result = LoadResult::InvalidHost;
        ESP_LOGW(TAG, "loaded %u bluetooth hosts, skipped %u invalid entries", static_cast<unsigned>(m_size), static_cast<unsigned>(skipped));
        return false;
    }

    ESP_LOGI(TAG, "loaded %u bluetooth hosts", static_cast<unsigned>(m_size));
    return true;
}

bool BtHostList::saveToMicroSd()
{
    m_last_result = LoadResult::Ok;

    if (m_destroyed)
    {
        m_last_result = LoadResult::Destroyed;
        ESP_LOGW(TAG, "not saving bluetooth host list after destructor");
        return false;
    }

    if (m_size == 0)
    {
        m_last_result = LoadResult::EmptyList;
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
        m_last_result = LoadResult::FileOpenFailed;
        ESP_LOGW(TAG, "could not open bluetooth host list for write: %s", m_path);
        return false;
    }

    const size_t bytes_written = serializeJsonPretty(doc, file);
    file.println();
    const bool synced = file.sync();
    file.close();

    if (bytes_written == 0 || !synced)
    {
        m_last_result = LoadResult::FileWriteFailed;
        ESP_LOGW(TAG, "could not write bluetooth host list: %s", m_path);
        return false;
    }

    ESP_LOGI(TAG, "saved %u bluetooth hosts to %s", static_cast<unsigned>(m_size), m_path);
    return true;
}

bool BtHostList::pruneBonds()
{
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
}

bool BtHostList::insert(const char* name, const esp_bd_addr_t bdaddr, uint8_t icon)
{
    m_last_result = LoadResult::Ok;

    bt_host_item_t* existing = find_host(m_head, bdaddr);
    if (existing)
    {
        existing->bonded = true;

        if ((!existing->name || existing->name[0] == '\0') && name && name[0] != '\0')
        {
            char* replacement = clone_string(name);
            if (!replacement)
            {
                m_last_result = LoadResult::AllocationFailed;
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
        m_last_result = LoadResult::AllocationFailed;
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
}

void BtHostList::clear()
{
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
}

size_t BtHostList::size() const
{
    return m_size;
}

bt_host_item_t* BtHostList::get(size_t index)
{
    bt_host_item_t* item = m_head;
    while (item && index > 0)
    {
        item = static_cast<bt_host_item_t*>(item->next_node);
        --index;
    }
    return item;
}

const bt_host_item_t* BtHostList::get(size_t index) const
{
    const bt_host_item_t* item = m_head;
    while (item && index > 0)
    {
        item = static_cast<const bt_host_item_t*>(item->next_node);
        --index;
    }
    return item;
}

bt_host_item_t* BtHostList::getFirstPhone()
{
    return const_cast<bt_host_item_t*>(find_host_by_icon(m_head, ICON_PHONE));
}

const bt_host_item_t* BtHostList::getFirstPhone() const
{
    return find_host_by_icon(m_head, ICON_PHONE);
}

bt_host_item_t* BtHostList::getFirstLaptop()
{
    return const_cast<bt_host_item_t*>(find_host_by_icon(m_head, ICON_LAPTOP));
}

const bt_host_item_t* BtHostList::getFirstLaptop() const
{
    return find_host_by_icon(m_head, ICON_LAPTOP);
}

BtHostList::LoadResult BtHostList::lastResult() const
{
    return m_last_result;
}

const char* BtHostList::lastResultName() const
{
    return result_name(m_last_result);
}
