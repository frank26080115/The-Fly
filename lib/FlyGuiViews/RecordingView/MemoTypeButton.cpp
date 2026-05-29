#include "MemoTypeButton.h"

#include "sprites.h"
#include "utilfuncs.h"

MemoTypeButton::MemoTypeButton(int16_t x, int16_t y)
    : FlyGuiItem(x, y, SPRITE_MEMOTYPE_NOTE_WIDTH, SPRITE_MEMOTYPE_NOTE_HEIGHT)
{
    setMemoType(MEMO_TYPE_NOTE);
}

void MemoTypeButton::setMemoType(MemoType type)
{
    if (type_ == type && !dirty())
    {
        return;
    }

    type_ = type;
    switch (type_)
    {
    case MEMO_TYPE_TODO:
        setSprite(sprite_memotype_todo, SPRITE_MEMOTYPE_TODO_WIDTH, SPRITE_MEMOTYPE_TODO_HEIGHT, SPRITE_MEMOTYPE_TODO_BYTES);
        break;
    case MEMO_TYPE_JOURNAL:
        setSprite(sprite_memotype_journal, SPRITE_MEMOTYPE_JOURNAL_WIDTH, SPRITE_MEMOTYPE_JOURNAL_HEIGHT, SPRITE_MEMOTYPE_JOURNAL_BYTES);
        break;
    case MEMO_TYPE_IDEA:
        setSprite(sprite_memotype_idea, SPRITE_MEMOTYPE_IDEA_WIDTH, SPRITE_MEMOTYPE_IDEA_HEIGHT, SPRITE_MEMOTYPE_IDEA_BYTES);
        break;
    case MEMO_TYPE_REMINDER:
        setSprite(sprite_memotype_reminder, SPRITE_MEMOTYPE_REMINDER_WIDTH, SPRITE_MEMOTYPE_REMINDER_HEIGHT, SPRITE_MEMOTYPE_REMINDER_BYTES);
        break;
    case MEMO_TYPE_NOTE:
    default:
        setSprite(sprite_memotype_note, SPRITE_MEMOTYPE_NOTE_WIDTH, SPRITE_MEMOTYPE_NOTE_HEIGHT, SPRITE_MEMOTYPE_NOTE_BYTES);
        break;
    }
}

void MemoTypeButton::cycleNext()
{
    switch (type_)
    {
    case MEMO_TYPE_NOTE:
        setMemoType(MEMO_TYPE_TODO);
        break;
    case MEMO_TYPE_TODO:
        setMemoType(MEMO_TYPE_JOURNAL);
        break;
    case MEMO_TYPE_JOURNAL:
        setMemoType(MEMO_TYPE_IDEA);
        break;
    case MEMO_TYPE_IDEA:
        setMemoType(MEMO_TYPE_REMINDER);
        break;
    case MEMO_TYPE_REMINDER:
    default:
        setMemoType(MEMO_TYPE_NOTE);
        break;
    }
}

char MemoTypeButton::typeCode() const
{
    return memo_type_to_code(type_);
}
