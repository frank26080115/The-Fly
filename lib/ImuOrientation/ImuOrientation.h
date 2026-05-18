#pragma once

#include <stdint.h>

namespace ImuOrientation
{

enum Orientation : uint8_t
{
    OrientationUnknown  = 0,
    OrientationFaceUp   = 1,
    OrientationFaceDown = 2,
    OrientationUpright  = 3,
    OrientationInverted = 4,
    OrientationEdge     = 5,
};

bool init();
bool poll();

uint8_t orientation();
bool    upsideDown();
int16_t rollDegrees();
int16_t pitchDegrees();

} // namespace ImuOrientation
