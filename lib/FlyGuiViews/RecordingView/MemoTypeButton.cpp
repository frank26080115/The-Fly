#include "MemoTypeButton.h"

#include "sprites.h"

MemoTypeButton::MemoTypeButton(int16_t x, int16_t y)
    : FlyGuiItem(x, y, SPRIT_MEMOTYPE_NOTE_WIDTH, SPRIT_MEMOTYPE_NOTE_HEIGHT)
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
        setSprite(sprit_memotype_todo, SPRIT_MEMOTYPE_TODO_WIDTH, SPRIT_MEMOTYPE_TODO_HEIGHT, SPRIT_MEMOTYPE_TODO_BYTES);
        break;
    case MEMO_TYPE_JOURNAL:
        setSprite(sprit_memotype_journal, SPRIT_MEMOTYPE_JOURNAL_WIDTH, SPRIT_MEMOTYPE_JOURNAL_HEIGHT, SPRIT_MEMOTYPE_JOURNAL_BYTES);
        break;
    case MEMO_TYPE_IDEA:
        setSprite(sprit_memotype_idea, SPRIT_MEMOTYPE_IDEA_WIDTH, SPRIT_MEMOTYPE_IDEA_HEIGHT, SPRIT_MEMOTYPE_IDEA_BYTES);
        break;
    case MEMO_TYPE_REMINDER:
        setSprite(sprit_memotype_reminder, SPRIT_MEMOTYPE_REMINDER_WIDTH, SPRIT_MEMOTYPE_REMINDER_HEIGHT, SPRIT_MEMOTYPE_REMINDER_BYTES);
        break;
    case MEMO_TYPE_NOTE:
    default:
        setSprite(sprit_memotype_note, SPRIT_MEMOTYPE_NOTE_WIDTH, SPRIT_MEMOTYPE_NOTE_HEIGHT, SPRIT_MEMOTYPE_NOTE_BYTES);
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
    switch (type_)
    {
    case MEMO_TYPE_TODO:
        return 'T';
    case MEMO_TYPE_JOURNAL:
        return 'J';
    case MEMO_TYPE_IDEA:
        return 'I';
    case MEMO_TYPE_REMINDER:
        return 'R';
    case MEMO_TYPE_NOTE:
    default:
        return 'M';
    }
}
