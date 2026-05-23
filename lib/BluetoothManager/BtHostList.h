#pragma once

#include "thefly_common.h"

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "esp_gap_bt_api.h"

static constexpr size_t kBtHostListMaxEntries = 8;
static constexpr size_t kBtHostNameMaxLength  = 32; // true max is ESP_BT_GAP_MAX_BDNAME_LEN + 1; but we only need to display it on a small LCD screen

#ifndef BUILD_WITH_SECURITY
typedef struct 
{
    esp_bd_addr_t bdaddr;
    char*         name;         // allocate, deallocate on destructor
    bool          bonded;       // use `bonded_mac_matches` to check
    uint8_t       icon;         // one of `ICON_*`
    void*         next_node;    // pointer to another bt_host_item_t
}
bt_host_item_t;
#else
typedef struct 
{
    esp_bd_addr_t bdaddr;            // all zeros indicate slot is empty
    char          name[kBtHostNameMaxLength];          // runtime display name, rebuilt from custom/reported names
    char          name_custom[kBtHostNameMaxLength];   // written through user admin interface, this has priority
    char          name_reported[kBtHostNameMaxLength]; // written only when pairing
    bool          bonded;            // use `bonded_mac_matches` to check, do not trust the value straight out of storage
    time_t        last_used;         // we have a fixed number of entries, if we are full and a pair happens, overwrite the oldest
    uint8_t       icon;              // one of `ICON_*`
    void*         next_node;         // runtime compatibility with linked-list callers; always null in fixed storage
}
bt_host_item_t;

typedef struct
{
    uint32_t       magic;
    uint32_t       version;
    uint8_t        count;
    bt_host_item_t host[kBtHostListMaxEntries];
}
bt_host_list_t;
#endif

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
    #ifdef BUILD_WITH_SECURITY
    bool loadFromNvs();
    bool saveToNvs();
    #endif
    bool pruneBonds();
    bool insert(const char* name, const esp_bd_addr_t bdaddr, uint8_t icon = ICON_BLUETOOTH);
    bool remove(size_t index, bool removeBond = true);
    void clear();

    size_t          size() const;
    bt_host_item_t* get(size_t index);
    const bt_host_item_t* get(size_t index) const;
    bt_host_item_t* getFirstPhone();
    const bt_host_item_t* getFirstPhone() const;
    bt_host_item_t* getFirstLaptop();
    const bt_host_item_t* getFirstLaptop() const;
    LoadResult      lastLoadResult() const;
    const char*     lastLoadResultName() const;

private:
    #ifndef BUILD_WITH_SECURITY
    static constexpr size_t kMaxPathLength = 96;
    char            m_path[kMaxPathLength];
    bt_host_item_t* m_head        = nullptr;
    bt_host_item_t* m_tail        = nullptr;
    size_t          m_size        = 0;
    #else
    bt_host_list_t  m_hosts       = {};
    size_t          m_size        = 0;
    #endif
    LoadResult      m_last_load_result = LoadResult::Ok;
    bool            m_destroyed   = false;
};
