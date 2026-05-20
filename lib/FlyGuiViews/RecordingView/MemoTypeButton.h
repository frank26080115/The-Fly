#pragma once

#include "../../FlyGui/FlyGui.h"
#include "defs.h"

class MemoTypeButton : public FlyGuiItem
{
public:
    MemoTypeButton(int16_t x, int16_t y);

    void     setMemoType(MemoType type);
    void     cycleNext();
    MemoType memoType() const
    {
        return type_;
    }
    char typeCode() const;

private:
    MemoType type_ = MemoType::Note;
};
