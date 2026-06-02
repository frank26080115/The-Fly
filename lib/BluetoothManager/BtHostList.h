#pragma once

/*
Policy about BtHostList

This list needs to work with the bonded devices list (esp_bt_gap_get_bond_device_list)

If an entry exists in BtHostList but not esp_bt_gap_get_bond_device_list, it is kept for showing in the web front-end,
but it does not show in the GUI ScrollView

If an entry exists in esp_bt_gap_get_bond_device_list but not in BtHostList, it does not show up on the web front-end
nor the GUI ScrollView

On boot, and on submission of new configuration from web front-end, if an entry exists in
esp_bt_gap_get_bond_device_list but not in BtHostList, then delete the bond with esp_bt_gap_remove_bond_device

Upon completion of bonding/pairing, an entry is entered into BtHostList

If the user edits a BDADDR to all 0 or blank in the web front-end, it means when it is submitted back, it is considered
not a valid entry.

If the user selects to unpair a device through the touchscreen GUI, unpair it by deleting the bond, but do not remove
the entry from BtHostList, as we might want to keep the customized name

Entries that are considered blank are never sent to the web front-end via the get_cfg request

Entries that are considered blank are never sent from the web front-end via the set_cfg request, but if encountered,
they are skipped and do not enter the table, and that request will blank out the remainder of the table after processing
the available JSON entries
*/

#include "thefly_common.h"

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "esp_gap_bt_api.h"

static constexpr size_t kBtHostListMaxEntries = 8;
static constexpr size_t kBtHostNameMaxLength =
    32; // true max is ESP_BT_GAP_MAX_BDNAME_LEN + 1; but we only need to display it on a small LCD screen

typedef struct
{
    esp_bd_addr_t bdaddr;                              // all zeros indicate slot is empty
    char          name_custom[kBtHostNameMaxLength];   // written through user admin interface, this has priority
    char          name_reported[kBtHostNameMaxLength]; // written only when pairing
    bool          bonded; // use `bonded_mac_matches` to check, do not trust the value straight out of storage
    time_t  last_used;    // we have a fixed number of entries, if we are full and a pair happens, overwrite the oldest
    uint8_t icon;         // one of `ICON_*`
} bt_host_item_t;

typedef struct
{
    uint32_t       magic;
    uint32_t       version;
    uint8_t        count;
    bt_host_item_t host[kBtHostListMaxEntries];
} bt_host_list_t;

inline const char* bt_host_display_name(const bt_host_item_t* item)
{
    if (!item)
    {
        return "";
    }

    return item->name_custom[0] != '\0' ? item->name_custom : item->name_reported;
}

inline const char* bt_host_display_name(const bt_host_item_t& item)
{
    return bt_host_display_name(&item);
}

class BtHostList
{
public:
    enum class LoadResult
    {
        Ok,
        SdNotReady,
        FileOpenFailed,
        FileTooLarge,
        FileReadFailed,
        JsonParseFailed,
        MissingHosts,
        InvalidHost,
        AllocationFailed,
        EmptyList,
        Destroyed,
        FileWriteFailed,
    };

    BtHostList();
    ~BtHostList();

    BtHostList(const BtHostList&)            = delete;
    BtHostList& operator=(const BtHostList&) = delete;

    bool loadFromMicroSd(const char* path = "/bluetooth.json");
    bool saveToMicroSd(bool allowEmpty = false);
    bool loadFromNvs();
    bool saveToNvs();
    bool copyHostList(bt_host_list_t& out) const;
    bool replaceHostList(const bt_host_list_t& hosts);
    bool pruneBonds();
    bool insert(const char* name, const esp_bd_addr_t bdaddr, uint8_t icon = ICON_UNKNOWN);
    bool unpair(size_t index);
    bool remove(size_t index, bool removeBond = true);
    void clear();

    size_t                size() const;
    bt_host_item_t*       get(size_t index);
    const bt_host_item_t* get(size_t index) const;
    bt_host_item_t*       getFirstPhone();
    const bt_host_item_t* getFirstPhone() const;
    bt_host_item_t*       getFirstLaptop();
    const bt_host_item_t* getFirstLaptop() const;
    LoadResult            lastLoadResult() const;
    const char*           lastLoadResultName() const;

private:
    bt_host_list_t m_hosts            = {};
    size_t         m_size             = 0;
    LoadResult     m_last_load_result = LoadResult::Ok;
    bool           m_destroyed        = false;
};
