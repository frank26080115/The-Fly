#include <Arduino.h>
#include <M5Unified.h>

#include "ImuOrientation.h"

namespace
{

constexpr const char* TAG             = "test_imu";
constexpr uint32_t    kReportPeriodMs = 200;

const char* orientation_name(uint8_t orientation)
{
    switch (static_cast<ImuOrientation::Orientation>(orientation))
    {
    case ImuOrientation::OrientationFaceUp:
        return "face_up";
    case ImuOrientation::OrientationFaceDown:
        return "face_down";
    case ImuOrientation::OrientationUpright:
        return "upright";
    case ImuOrientation::OrientationInverted:
        return "inverted";
    case ImuOrientation::OrientationEdge:
        return "edge";
    case ImuOrientation::OrientationUnknown:
    default:
        return "unknown";
    }
}

} // namespace

void test_imu()
{
    Serial.begin(115200);
    delay(1000);

    auto cfg          = M5.config();
    cfg.internal_imu  = true;
    cfg.external_imu  = false;
    M5.begin(cfg);

    Serial.println();
    Serial.printf("%s: starting IMU orientation test\n", TAG);

    const bool init_ok = ImuOrientation::init();
    Serial.printf("%s: init=%u imu_enabled=%u\n", TAG, init_ok ? 1U : 0U, M5.Imu.isEnabled() ? 1U : 0U);

    uint32_t report_started_ms = millis();
    uint32_t poll_count        = 0;
    uint32_t sample_count      = 0;

    while (true)
    {
        M5.update();
        ++poll_count;

        if (ImuOrientation::poll())
        {
            ++sample_count;
        }

        const uint32_t now_ms = millis();
        if (static_cast<uint32_t>(now_ms - report_started_ms) >= kReportPeriodMs)
        {
            const uint8_t orientation = ImuOrientation::orientation();
            Serial.printf("%s: ms=%lu polls=%lu samples=%lu imu_enabled=%u orientation=%u(%s) upside_down=%u roll_deg=%d pitch_deg=%d\n",
                          TAG,
                          static_cast<unsigned long>(now_ms),
                          static_cast<unsigned long>(poll_count),
                          static_cast<unsigned long>(sample_count),
                          M5.Imu.isEnabled() ? 1U : 0U,
                          static_cast<unsigned>(orientation),
                          orientation_name(orientation),
                          ImuOrientation::upsideDown() ? 1U : 0U,
                          static_cast<int>(ImuOrientation::rollDegrees()),
                          static_cast<int>(ImuOrientation::pitchDegrees()));

            poll_count        = 0;
            sample_count      = 0;
            report_started_ms = now_ms;
        }

        delay(1);
    }
}
