#pragma once

#include <stdint.h>

/*
use this file to specify pin mappings
*/

constexpr int kNS4168SpeakerBclk = 12;
constexpr int kNS4168SpeakerLrck = 0;
constexpr int kNS4168SpeakerDout = 2;
constexpr int kSPM1423MicClk     = 0;
constexpr int kSPM1423MicDin     = 34;

constexpr uint8_t kAxp192Address      = 0x34;
constexpr uint8_t kAxp192Gpio2Control = 0x93;
constexpr int     kInternalI2cSda     = 21;
constexpr int     kInternalI2cScl     = 22;

constexpr int kCore2SdSclk = 18;
constexpr int kCore2SdMiso = 38;
constexpr int kCore2SdMosi = 23;
constexpr int kCore2SdCs   = 4;
