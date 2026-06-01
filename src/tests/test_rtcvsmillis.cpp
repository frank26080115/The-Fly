#include <Arduino.h>
#include <M5Unified.h>

namespace
{

constexpr const char* TAG             = "test_rtcvsmillis";
constexpr uint32_t    kReportPeriodMs = 5000;

void print_rtc_vs_millis()
{
    m5::rtc_datetime_t now = {};
    const uint32_t     now_ms = millis();
    const bool         rtc_ok = M5.Rtc.getDateTime(&now);

    if (!rtc_ok)
    {
        Serial.printf("%s: rtc_read=failed millis=%lu\n",
                      TAG,
                      static_cast<unsigned long>(now_ms));
        return;
    }

    Serial.printf("%s: rtc=%04d-%02d-%02d %02d:%02d:%02d millis=%lu\n",
                  TAG,
                  static_cast<int>(now.date.year),
                  static_cast<int>(now.date.month),
                  static_cast<int>(now.date.date),
                  static_cast<int>(now.time.hours),
                  static_cast<int>(now.time.minutes),
                  static_cast<int>(now.time.seconds),
                  static_cast<unsigned long>(now_ms));
}

} // namespace

void test_rtcvsmillis()
{
    Serial.begin(115200);
    delay(250);

    if (!M5.Rtc.isEnabled())
    {
        auto cfg         = M5.config();
        cfg.internal_rtc = true;
        cfg.external_rtc = false;
        M5.begin(cfg);
    }

    Serial.println();
    Serial.printf("%s: starting real RTC vs millis test rtc_enabled=%u\n",
                  TAG,
                  M5.Rtc.isEnabled() ? 1U : 0U);
    print_rtc_vs_millis();

    uint32_t last_report_ms = millis();
    while (true)
    {
        M5.update();

        const uint32_t now_ms = millis();
        if (static_cast<uint32_t>(now_ms - last_report_ms) >= kReportPeriodMs)
        {
            print_rtc_vs_millis();
            last_report_ms = now_ms;
        }

        delay(1);
    }
}
