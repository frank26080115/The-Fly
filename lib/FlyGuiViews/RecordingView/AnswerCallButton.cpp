#include "AnswerCallButton.h"

#include "sprites.h"

AnswerCallButton::AnswerCallButton(int16_t x, int16_t y) : FlyGuiItem(x, y, SPRITE_PICKUP_WIDTH, SPRITE_PICKUP_HEIGHT)
{
    setSprite(sprite_pickupdown, SPRITE_PICKUPDOWN_WIDTH, SPRITE_PICKUPDOWN_HEIGHT, SPRITE_PICKUPDOWN_BYTES);
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
        setSprite(sprite_pickup, SPRITE_PICKUP_WIDTH, SPRITE_PICKUP_HEIGHT, SPRITE_PICKUP_BYTES);
    }
    else
    {
        setSprite(sprite_pickupdown, SPRITE_PICKUPDOWN_WIDTH, SPRITE_PICKUPDOWN_HEIGHT, SPRITE_PICKUPDOWN_BYTES);
    }
}
