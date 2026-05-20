#include "MemoTypeButton.h"

#include "sprites.h"

MemoTypeButton::MemoTypeButton(int16_t x, int16_t y)
    : FlyGuiItem(x, y, SPRIT_MEMOTYPE_NOTE_WIDTH, SPRIT_MEMOTYPE_NOTE_HEIGHT)
{
    setMemoType(MemoType::Note);
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
    case MemoType::Todo:
        setSprite(sprit_memotype_todo, SPRIT_MEMOTYPE_TODO_WIDTH, SPRIT_MEMOTYPE_TODO_HEIGHT, SPRIT_MEMOTYPE_TODO_BYTES);
        break;
    case MemoType::Journal:
        setSprite(sprit_memotype_journal, SPRIT_MEMOTYPE_JOURNAL_WIDTH, SPRIT_MEMOTYPE_JOURNAL_HEIGHT, SPRIT_MEMOTYPE_JOURNAL_BYTES);
        break;
    case MemoType::Idea:
        setSprite(sprit_memotype_idea, SPRIT_MEMOTYPE_IDEA_WIDTH, SPRIT_MEMOTYPE_IDEA_HEIGHT, SPRIT_MEMOTYPE_IDEA_BYTES);
        break;
    case MemoType::Reminder:
        setSprite(sprit_memotype_reminder, SPRIT_MEMOTYPE_REMINDER_WIDTH, SPRIT_MEMOTYPE_REMINDER_HEIGHT, SPRIT_MEMOTYPE_REMINDER_BYTES);
        break;
    case MemoType::Note:
    default:
        setSprite(sprit_memotype_note, SPRIT_MEMOTYPE_NOTE_WIDTH, SPRIT_MEMOTYPE_NOTE_HEIGHT, SPRIT_MEMOTYPE_NOTE_BYTES);
        break;
    }
}

void MemoTypeButton::cycleNext()
{
    switch (type_)
    {
    case MemoType::Note:
        setMemoType(MemoType::Todo);
        break;
    case MemoType::Todo:
        setMemoType(MemoType::Journal);
        break;
    case MemoType::Journal:
        setMemoType(MemoType::Idea);
        break;
    case MemoType::Idea:
        setMemoType(MemoType::Reminder);
        break;
    case MemoType::Reminder:
    default:
        setMemoType(MemoType::Note);
        break;
    }
}

char MemoTypeButton::typeCode() const
{
    switch (type_)
    {
    case MemoType::Todo:
        return 'T';
    case MemoType::Journal:
        return 'J';
    case MemoType::Idea:
        return 'I';
    case MemoType::Reminder:
        return 'R';
    case MemoType::Note:
    default:
        return 'M';
    }
}
