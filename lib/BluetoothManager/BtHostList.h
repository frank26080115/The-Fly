#pragma once

#include <stddef.h>

#include "defs.h"
#include "esp_gap_bt_api.h"

typedef struct 
{
    esp_bd_addr_t bdaddr;
    char*         name;         // allocate, deallocate on destructor
    bool          bonded;       // use `bonded_mac_matches` to check
    uint8_t       icon;         // one of `ICON_*`
    void*         next_node;    // pointer to another bt_host_item_t
}
bt_host_item_t;

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
    bool saveToMicroSd();
    bool insert(const char* name, const esp_bd_addr_t bdaddr, uint8_t icon = ICON_BLUETOOTH);
    void clear();

    size_t          size() const;
    bt_host_item_t* get(size_t index);
    const bt_host_item_t* get(size_t index) const;
    LoadResult      lastResult() const;
    const char*     lastResultName() const;

private:
    static constexpr size_t kMaxPathLength = 96;

    bt_host_item_t* m_head        = nullptr;
    bt_host_item_t* m_tail        = nullptr;
    size_t          m_size        = 0;
    LoadResult      m_last_result = LoadResult::Ok;
    bool            m_destroyed   = false;
    char            m_path[kMaxPathLength];
};
