#pragma once

#include "../FlyGui/FlyGui.h"
#include <stddef.h>

class ScrollView : public FlyGuiView
{
public:
    explicit ScrollView(uint16_t viewId = FLYGUI_VIEW_SCROLL, FlyGuiItemCallback exitCallback = nullptr);

    // Planned callers can populate this view from JSON-backed records by looking
    // up each record's icon through IconLookup, assigning that sprite to a
    // FlyGuiItem, and adding it here. Bluetooth can append known hosts plus
    // pairing/prune actions, Wi-Fi can append station/AP choices, and cloud can
    // append endpoint/NTP/info actions.
    void addItem(FlyGuiItem& item);
    void removeAllItems();

    size_t itemCount() const
    {
        return itemCount_;
    }
    size_t selectedIndex() const
    {
        return selectedIndex_;
    }
    FlyGuiItem* selectedItem() const;

    void setExitCallback(FlyGuiItemCallback exitCallback);
    void selectIndex(size_t index);
    void scrollLeft();
    void scrollRight();

    void onLoad() override;
    bool handleTouch(const FlyGuiTouchEvent& event) override;
    void redraw(bool forced) override;
    void onPressLeft() override;
    void onPressMid() override;
    void onPressRight() override;

private:
    enum Slot
    {
        SLOT_LEFT,
        SLOT_CENTER,
        SLOT_RIGHT,
    };

    FlyGuiItem* itemAt(size_t index) const;
    FlyGuiItem* itemAtWrapped(int32_t index) const;
    bool        containsSlot(Slot slot, int16_t x, int16_t y) const;
    void        drawContent();
    void        drawItemInSlot(FlyGuiItem& item, Slot slot, bool faded);
    void        drawSelectedText();
    void        drawExitButton();

    FlyGuiItem      exitItem_;
    FlyGuiItemCallback exitCallback_ = nullptr;
    size_t          itemCount_       = 0;
    size_t          selectedIndex_   = 0;
};
