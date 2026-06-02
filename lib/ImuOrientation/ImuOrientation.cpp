/*
Originally I was going to use the device orientation reported by the IMU to perform GUI interactions
such as displaying information when the screen is upside down

But... it turns out the IMU is mounted on a daughter-board
so if I want to add another I2S codec, the IMU might disappear
*/

#include "ImuOrientation.h"

#include <M5Unified.h>
#include <math.h>

namespace ImuOrientation
{
namespace
{

constexpr uint32_t kPollIntervalMs = 100;
constexpr float    kAccelLpfAlpha  = 0.25f;
constexpr float    kRadToDeg       = 57.2957795f;

// A 20 degree hysteresis band around the 45 degree face boundary.
constexpr float kKeepOrientationScore   = 0.5735764f; // cos(55 degrees)
constexpr float kSwitchOrientationScore = 0.8191521f; // cos(35 degrees)

bool        g_initialized   = false;
bool        g_have_sample   = false;
bool        g_upside_down   = false;
uint32_t    g_last_poll_ms  = 0;
float       g_accel_x       = 0.0f;
float       g_accel_y       = 0.0f;
float       g_accel_z       = 0.0f;
int16_t     g_roll_degrees  = 0;
int16_t     g_pitch_degrees = 0;
Orientation g_orientation   = OrientationUnknown;

int16_t rounded_degrees(float degrees)
{
    if (degrees > 32767.0f)
    {
        return 32767;
    }
    if (degrees < -32768.0f)
    {
        return -32768;
    }
    return static_cast<int16_t>(degrees >= 0.0f ? degrees + 0.5f : degrees - 0.5f);
}

void update_filtered_accel(float x, float y, float z)
{
    if (!g_have_sample)
    {
        g_accel_x     = x;
        g_accel_y     = y;
        g_accel_z     = z;
        g_have_sample = true;
        return;
    }

    g_accel_x += (x - g_accel_x) * kAccelLpfAlpha;
    g_accel_y += (y - g_accel_y) * kAccelLpfAlpha;
    g_accel_z += (z - g_accel_z) * kAccelLpfAlpha;
}

float score_for_orientation(Orientation orientation, float x, float y, float z, float magnitude)
{
    if (magnitude <= 0.0f)
    {
        return -1.0f;
    }

    switch (orientation)
    {
    case OrientationFaceUp:
        return z / magnitude;
    case OrientationFaceDown:
        return -z / magnitude;
    case OrientationUpright:
        return y / magnitude;
    case OrientationInverted:
        return -y / magnitude;
    case OrientationEdge:
        return fabsf(x) / magnitude;
    case OrientationUnknown:
    default:
        return -1.0f;
    }
}

Orientation candidate_orientation(float x, float y, float z, float magnitude, float& candidate_score)
{
    Orientation best       = OrientationFaceUp;
    float       best_score = score_for_orientation(best, x, y, z, magnitude);

    const Orientation candidates[] = {
        OrientationFaceDown,
        OrientationUpright,
        OrientationInverted,
        OrientationEdge,
    };

    for (Orientation candidate : candidates)
    {
        const float score = score_for_orientation(candidate, x, y, z, magnitude);
        if (score > best_score)
        {
            best       = candidate;
            best_score = score;
        }
    }

    candidate_score = best_score;
    return best;
}

void update_orientation(float x, float y, float z)
{
    const float magnitude = sqrtf((x * x) + (y * y) + (z * z));
    if (magnitude < 0.001f)
    {
        return;
    }

    float             candidate_score = 0.0f;
    const Orientation candidate       = candidate_orientation(x, y, z, magnitude, candidate_score);
    if (g_orientation == OrientationUnknown)
    {
        g_orientation = candidate;
    }
    else if (candidate != g_orientation)
    {
        const float current_score = score_for_orientation(g_orientation, x, y, z, magnitude);
        if (current_score < kKeepOrientationScore || candidate_score >= kSwitchOrientationScore)
        {
            g_orientation = candidate;
        }
    }

    if (g_orientation == OrientationFaceDown || g_orientation == OrientationInverted)
    {
        g_upside_down = true;
    }
    else if (g_orientation == OrientationUpright)
    {
        g_upside_down = false;
    }
}

void update_angles(float x, float y, float z)
{
    const float roll  = atan2f(y, z) * kRadToDeg;
    const float pitch = atan2f(-x, sqrtf((y * y) + (z * z))) * kRadToDeg;

    g_roll_degrees  = rounded_degrees(roll);
    g_pitch_degrees = rounded_degrees(pitch);
}

} // namespace

bool init()
{
    if (!M5.Imu.isEnabled())
    {
        if (M5.In_I2C.isEnabled())
        {
            M5.Imu.begin(&M5.In_I2C, M5.getBoard());
        }
        if (!M5.Imu.isEnabled() && M5.Ex_I2C.isEnabled())
        {
            M5.Imu.begin(&M5.Ex_I2C);
        }
    }

    g_initialized = M5.Imu.isEnabled();
    return g_initialized;
}

bool poll()
{
    if (!g_initialized && !init())
    {
        return false;
    }

    const uint32_t now_ms = millis();
    if (g_have_sample && static_cast<uint32_t>(now_ms - g_last_poll_ms) < kPollIntervalMs)
    {
        return false;
    }

    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    if (!M5.Imu.getAccel(&x, &y, &z))
    {
        return false;
    }

    g_last_poll_ms = now_ms;
    update_filtered_accel(x, y, z);
    update_angles(g_accel_x, g_accel_y, g_accel_z);
    update_orientation(g_accel_x, g_accel_y, g_accel_z);
    return true;
}

uint8_t orientation()
{
    return static_cast<uint8_t>(g_orientation);
}

bool upsideDown()
{
    return g_upside_down;
}

int16_t rollDegrees()
{
    return g_roll_degrees;
}

int16_t pitchDegrees()
{
    return g_pitch_degrees;
}

} // namespace ImuOrientation
