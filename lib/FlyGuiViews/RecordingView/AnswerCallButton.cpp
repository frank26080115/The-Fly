#include "AnswerCallButton.h"

#include "sprites.h"

AnswerCallButton::AnswerCallButton(int16_t x, int16_t y)
    : FlyGuiItem(x, y, SPRIT_PICKUP_WIDTH, SPRIT_PICKUP_HEIGHT)
{
    setSprite(sprit_pickupdown, SPRIT_PICKUPDOWN_WIDTH, SPRIT_PICKUPDOWN_HEIGHT, SPRIT_PICKUPDOWN_BYTES);
}

void AnswerCallButton::setPhoneUiState(CallManager::PhoneUiState state)
{
    if (state_ == state)
    {
        return;
    }

    state_ = state;
    if (state_ == CallManager::PhoneUiState::IncomingCall)
    {
        setSprite(sprit_pickup, SPRIT_PICKUP_WIDTH, SPRIT_PICKUP_HEIGHT, SPRIT_PICKUP_BYTES);
    }
    else
    {
        setSprite(sprit_pickupdown, SPRIT_PICKUPDOWN_WIDTH, SPRIT_PICKUPDOWN_HEIGHT, SPRIT_PICKUPDOWN_BYTES);
    }
}
