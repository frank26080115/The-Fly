#include "ScrollView.h"

#include "FlyGuiText.h"
#include "sprites.h"
#include <string.h>

namespace
{
constexpr int16_t kItemWidth      = 106;
constexpr int16_t kItemHeight     = 114;
constexpr int16_t kItemY          = 20;
constexpr int16_t kTextY          = 140;
constexpr int16_t kTextMaxY       = 188;
constexpr int16_t kTextLineHeight = 16;
constexpr int16_t kExitSize       = 50;
constexpr int16_t kExitY          = 190;
constexpr float   kTextSize       = 1.5f;
constexpr uint8_t kTextFont       = 1;

int16_t slot_x(int slot)
{
    switch (slot)
    {
    case 0:
        return 1;
    case 1:
        return static_cast<int16_t>((thefly_display.width() - kItemWidth) / 2);
    case 2:
    default:
        return static_cast<int16_t>(thefly_display.width() - kItemWidth - 1);
    }
}

int16_t exit_x()
{
    return static_cast<int16_t>((thefly_display.width() - kExitSize) / 2);
}

void draw_centered_line(char* line, size_t& len, int16_t& y)
{
    while (len > 0 && line[0] == ' ')
    {
        memmove(line, line + 1, len);
        --len;
        line[len] = '\0';
    }

    while (len > 0 && line[len - 1] == ' ')
    {
        line[--len] = '\0';
    }

    if (len == 0 || y + kTextLineHeight > kTextMaxY)
    {
        return;
    }

    thefly_display.drawString(line, thefly_display.width() / 2, y);
    y += kTextLineHeight;
    len     = 0;
    line[0] = '\0';
}

void draw_centered_wrapped_text(const char* text)
{
    if (!text || text[0] == '\0')
    {
        return;
    }

    constexpr size_t kLineMax = 128;
    constexpr size_t kNoSpace = static_cast<size_t>(-1);

    const int16_t width = static_cast<int16_t>(thefly_display.width() - 8);
    int16_t       y     = kTextY;
    char          line[kLineMax] = {};
    size_t        len            = 0;
    size_t        lastSpace      = kNoSpace;

    for (const char* p = text; *p; ++p)
    {
        const char c = *p;
        if (c == '\r')
        {
            continue;
        }

        if (c == '\n')
        {
            draw_centered_line(line, len, y);
            lastSpace = kNoSpace;
            continue;
        }

        if (len + 1 >= sizeof(line))
        {
            draw_centered_line(line, len, y);
            lastSpace = kNoSpace;
        }

        line[len++] = c;
        line[len]   = '\0';
        if (c == ' ')
        {
            lastSpace = len - 1;
        }

        while (thefly_display.textWidth(line) > width && len > 1)
        {
            if (lastSpace != kNoSpace && lastSpace > 0)
            {
                char remainder[kLineMax] = {};
                strncpy(remainder, line + lastSpace + 1, sizeof(remainder) - 1);
                line[lastSpace] = '\0';
                size_t drawLen  = lastSpace;
                draw_centered_line(line, drawLen, y);
                strncpy(line, remainder, sizeof(line) - 1);
                line[sizeof(line) - 1] = '\0';
                len                    = strlen(line);
                lastSpace              = kNoSpace;
                for (size_t i = 0; i < len; ++i)
                {
                    if (line[i] == ' ')
                    {
                        lastSpace = i;
                    }
                }
            }
            else
            {
                const char overflow = line[len - 1];
                line[len - 1]       = '\0';
                size_t drawLen      = len - 1;
                draw_centered_line(line, drawLen, y);
                line[0]   = overflow;
                line[1]   = '\0';
                len       = 1;
                lastSpace = overflow == ' ' ? 0 : kNoSpace;
            }
        }
    }

    if (len > 0)
    {
        draw_centered_line(line, len, y);
    }
}
} // namespace

ScrollView::ScrollView(uint16_t viewId, FlyGuiItemCallback exitCallback)
    : FlyGuiView(viewId),
      exitItem_(exit_x(), kExitY, kExitSize, kExitSize),
      exitCallback_(exitCallback)
{
    exitItem_.setSprite(sprit_exit_50, SPRIT_EXIT_50_WIDTH, SPRIT_EXIT_50_HEIGHT, SPRIT_EXIT_50_BYTES);
    exitItem_.setCallback(exitCallback_);
}

void ScrollView::addItem(FlyGuiItem& item)
{
    FlyGuiView::addItem(item);
    ++itemCount_;
    setDirty();
}

void ScrollView::removeAllItems()
{
    FlyGuiView::removeAllItems();
    itemCount_     = 0;
    selectedIndex_ = 0;
    setDirty();
}

FlyGuiItem* ScrollView::selectedItem() const
{
    return itemAt(selectedIndex_);
}

void ScrollView::setExitCallback(FlyGuiItemCallback exitCallback)
{
    exitCallback_ = exitCallback;
    exitItem_.setCallback(exitCallback_);
}

void ScrollView::selectIndex(size_t index)
{
    if (itemCount_ == 0)
    {
        selectedIndex_ = 0;
        setDirty();
        return;
    }

    const size_t wrapped = index % itemCount_;
    if (selectedIndex_ != wrapped)
    {
        selectedIndex_ = wrapped;
        setDirty();
    }
}

void ScrollView::scrollLeft()
{
    if (itemCount_ <= 1)
    {
        return;
    }

    selectedIndex_ = selectedIndex_ == 0 ? itemCount_ - 1 : selectedIndex_ - 1;
    setDirty();
}

void ScrollView::scrollRight()
{
    if (itemCount_ <= 1)
    {
        return;
    }

    selectedIndex_ = (selectedIndex_ + 1) % itemCount_;
    setDirty();
}

void ScrollView::onLoad()
{
    selectedIndex_ = 0;
    for (FlyGuiItem* item = firstItem(); item; item = item->next())
    {
        item->onLoad();
    }
    exitItem_.onLoad();
    setDirty();
}

bool ScrollView::handleTouch(const FlyGuiTouchEvent& event)
{
    if (exitItem_.contains(event.x, event.y) || exitItem_.isPressed())
    {
        return exitItem_.handleTouch(event);
    }

    FlyGuiItem* selected = selectedItem();
    if (selected && (containsSlot(SLOT_CENTER, event.x, event.y) || selected->isPressed()))
    {
        return selected->handleTouch(event);
    }

    if (event.justPressed && containsSlot(SLOT_LEFT, event.x, event.y))
    {
        scrollLeft();
        return true;
    }

    if (event.justPressed && containsSlot(SLOT_RIGHT, event.x, event.y))
    {
        scrollRight();
        return true;
    }

    return false;
}

void ScrollView::redraw(bool forced)
{
    bool itemDirty = exitItem_.dirty();
    for (FlyGuiItem* item = firstItem(); item && !itemDirty; item = item->next())
    {
        itemDirty = item->dirty();
    }

    if (!forced && !dirty() && !itemDirty)
    {
        return;
    }

    drawContent();
    markClean();
}

void ScrollView::onPressLeft()
{
    scrollLeft();
}

void ScrollView::onPressMid()
{
    exitItem_.trigger();
}

void ScrollView::onPressRight()
{
    scrollRight();
}

FlyGuiItem* ScrollView::itemAt(size_t index) const
{
    size_t current = 0;
    for (FlyGuiItem* item = firstItem(); item; item = item->next())
    {
        if (current == index)
        {
            return item;
        }
        ++current;
    }

    return nullptr;
}

FlyGuiItem* ScrollView::itemAtWrapped(int32_t index) const
{
    if (itemCount_ == 0)
    {
        return nullptr;
    }

    while (index < 0)
    {
        index += static_cast<int32_t>(itemCount_);
    }

    return itemAt(static_cast<size_t>(index) % itemCount_);
}

bool ScrollView::containsSlot(Slot slot, int16_t x, int16_t y) const
{
    const int16_t sx = slot_x(slot);
    return x >= sx && y >= kItemY && x < sx + kItemWidth && y < kItemY + kItemHeight;
}

void ScrollView::drawContent()
{
    thefly_display.fillRect(0,
                            FlyGui::topBarHeight(),
                            thefly_display.width(),
                            static_cast<int16_t>(thefly_display.height() - FlyGui::topBarHeight()),
                            TFT_BLACK);

    if (itemCount_ > 0)
    {
        if (itemCount_ == 1)
        {
            FlyGuiItem* only = selectedItem();
            if (only)
            {
                drawItemInSlot(*only, SLOT_LEFT, true);
                drawItemInSlot(*only, SLOT_RIGHT, true);
            }
        }
        else
        {
            FlyGuiItem* left = itemAtWrapped(static_cast<int32_t>(selectedIndex_) - 1);
            if (left)
            {
                drawItemInSlot(*left, SLOT_LEFT, true);
            }

            FlyGuiItem* right = itemAtWrapped(static_cast<int32_t>(selectedIndex_) + 1);
            if (right)
            {
                drawItemInSlot(*right, SLOT_RIGHT, true);
            }
        }

        FlyGuiItem* center = selectedItem();
        if (center)
        {
            drawItemInSlot(*center, SLOT_CENTER, false);
        }

        drawSelectedText();
    }

    drawExitButton();
}

void ScrollView::drawItemInSlot(FlyGuiItem& item, Slot slot, bool faded)
{
    item.relocate(slot_x(slot), kItemY, kItemWidth, kItemHeight);
    item.setFaded(faded);
    item.redraw(true);
}

void ScrollView::drawSelectedText()
{
    const FlyGuiItem* item = selectedItem();
    const char*       text = item ? item->mainText() : nullptr;
    if (!text || text[0] == '\0')
    {
        return;
    }

    thefly_display.fillRect(0, kTextY, thefly_display.width(), static_cast<int16_t>(kTextMaxY - kTextY), TFT_BLACK);
    thefly_display.setTextFont(kTextFont);
    thefly_display.setTextSize(kTextSize);
    thefly_display.setTextDatum(top_center);
    thefly_display.setTextColor(TFT_WHITE, TFT_BLACK);
    draw_centered_wrapped_text(text);
}

void ScrollView::drawExitButton()
{
    exitItem_.relocate(exit_x(), kExitY, kExitSize, kExitSize);
    exitItem_.redraw(true);
}
