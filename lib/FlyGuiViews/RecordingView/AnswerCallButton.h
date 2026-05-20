#pragma once

#include "../../FlyGui/FlyGui.h"
#include "CallManager.h"

class AnswerCallButton : public FlyGuiItem
{
public:
    AnswerCallButton(int16_t x, int16_t y);

    void setPhoneUiState(CallManager::PhoneUiState state);

private:
    CallManager::PhoneUiState state_ = CallManager::PhoneUiState::Idle;
};
