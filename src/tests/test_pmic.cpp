#include <Arduino.h>
#include <M5Unified.h>

namespace
{

constexpr const char* TAG             = "test_pmic";
constexpr uint32_t    kReportPeriodMs = 1000;
constexpr int16_t     kColumnCount    = 2;
constexpr int16_t     kMarginX        = 2;
constexpr int16_t     kMarginY        = 2;
constexpr int16_t     kLineGap        = 1;

struct PmicSnapshot
{
    uint32_t                       now_ms         = 0;
    m5::Power_Class::pmic_t        pmic_type      = m5::Power_Class::pmic_unknown;
    m5::Power_Class::is_charging_t charging       = m5::Power_Class::charge_unknown;
    int32_t                        battery_pct    = 0;
    int16_t                        battery_mv     = 0;
    int32_t                        battery_ma     = 0;
    int16_t                        vbus_mv        = 0;
    bool                           ext_output     = false;
    bool                           usb_output     = false;
    bool                           has_axp_detail = false;
    bool                           axp_acin       = false;
    bool                           axp_vbus       = false;
    float                          acin_mv        = 0.0f;
    float                          acin_ma        = 0.0f;
    float                          axp_vbus_mv    = 0.0f;
    float                          axp_vbus_ma    = 0.0f;
    float                          battery_chg_ma = 0.0f;
    float                          battery_dis_ma = 0.0f;
};

const char* pmic_type_name(m5::Power_Class::pmic_t type)
{
    switch (type)
    {
    case m5::Power_Class::pmic_adc:
        return "adc";
    case m5::Power_Class::pmic_axp192:
        return "axp192";
    case m5::Power_Class::pmic_ip5306:
        return "ip5306";
    case m5::Power_Class::pmic_axp2101:
        return "axp2101";
    case m5::Power_Class::pmic_aw32001:
        return "aw32001";
    case m5::Power_Class::pmic_py32pmic:
        return "py32pmic";
    case m5::Power_Class::pmic_m5pm1:
        return "m5pm1";
    case m5::Power_Class::pmic_unknown:
    default:
        return "unknown";
    }
}

const char* charging_name(m5::Power_Class::is_charging_t charging)
{
    switch (charging)
    {
    case m5::Power_Class::is_discharging:
        return "discharging";
    case m5::Power_Class::is_charging:
        return "charging";
    case m5::Power_Class::charge_unknown:
    default:
        return "unknown";
    }
}

const char* charging_short_name(m5::Power_Class::is_charging_t charging)
{
    switch (charging)
    {
    case m5::Power_Class::is_discharging:
        return "dis";
    case m5::Power_Class::is_charging:
        return "chg";
    case m5::Power_Class::charge_unknown:
    default:
        return "unk";
    }
}

void read_axp192_detail(PmicSnapshot& snapshot)
{
#if !defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(CONFIG_IDF_TARGET_ESP32C3) &&                                      \
    !defined(CONFIG_IDF_TARGET_ESP32C6) && !defined(CONFIG_IDF_TARGET_ESP32P4)
    snapshot.has_axp_detail = true;
    snapshot.axp_acin       = M5.Power.Axp192.isACIN();
    snapshot.axp_vbus       = M5.Power.Axp192.isVBUS();
    snapshot.acin_mv        = M5.Power.Axp192.getACINVoltage() * 1000.0f;
    snapshot.acin_ma        = M5.Power.Axp192.getACINCurrent();
    snapshot.axp_vbus_mv    = M5.Power.Axp192.getVBUSVoltage() * 1000.0f;
    snapshot.axp_vbus_ma    = M5.Power.Axp192.getVBUSCurrent();
    snapshot.battery_chg_ma = M5.Power.Axp192.getBatteryChargeCurrent();
    snapshot.battery_dis_ma = M5.Power.Axp192.getBatteryDischargeCurrent();
#else
    (void)snapshot;
#endif
}

void read_axp2101_detail(PmicSnapshot& snapshot)
{
#if !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32C6) && !defined(CONFIG_IDF_TARGET_ESP32P4)
    snapshot.has_axp_detail = true;
    snapshot.axp_acin       = M5.Power.Axp2101.isACIN();
    snapshot.axp_vbus       = M5.Power.Axp2101.isVBUS();
    snapshot.acin_mv        = M5.Power.Axp2101.getACINVoltage() * 1000.0f;
    snapshot.acin_ma        = M5.Power.Axp2101.getACINCurrent();
    snapshot.axp_vbus_mv    = M5.Power.Axp2101.getVBUSVoltage() * 1000.0f;
    snapshot.axp_vbus_ma    = M5.Power.Axp2101.getVBUSCurrent();
    snapshot.battery_chg_ma = M5.Power.Axp2101.getBatteryChargeCurrent();
    snapshot.battery_dis_ma = M5.Power.Axp2101.getBatteryDischargeCurrent();
#else
    (void)snapshot;
#endif
}

PmicSnapshot read_pmic()
{
    PmicSnapshot snapshot;
    snapshot.now_ms      = millis();
    snapshot.pmic_type   = M5.Power.getType();
    snapshot.charging    = M5.Power.isCharging();
    snapshot.battery_pct = M5.Power.getBatteryLevel();
    snapshot.battery_mv  = M5.Power.getBatteryVoltage();
    snapshot.battery_ma  = M5.Power.getBatteryCurrent();
    snapshot.vbus_mv     = M5.Power.getVBUSVoltage();
    snapshot.ext_output  = M5.Power.getExtOutput();
    snapshot.usb_output  = M5.Power.getUsbOutput();

    switch (snapshot.pmic_type)
    {
    case m5::Power_Class::pmic_axp192:
        read_axp192_detail(snapshot);
        break;
    case m5::Power_Class::pmic_axp2101:
        read_axp2101_detail(snapshot);
        break;
    default:
        break;
    }

    return snapshot;
}

void print_pmic_line(const PmicSnapshot& snapshot)
{
    Serial.printf(
        "%s: ms=%lu pmic=%s(%u) charging=%s(%u) batt_pct=%ld batt_mv=%d batt_ma=%ld vbus_mv=%d ext_out=%u usb_out=%u",
        TAG,
        static_cast<unsigned long>(snapshot.now_ms),
        pmic_type_name(snapshot.pmic_type),
        static_cast<unsigned>(snapshot.pmic_type),
        charging_name(snapshot.charging),
        static_cast<unsigned>(snapshot.charging),
        static_cast<long>(snapshot.battery_pct),
        static_cast<int>(snapshot.battery_mv),
        static_cast<long>(snapshot.battery_ma),
        static_cast<int>(snapshot.vbus_mv),
        snapshot.ext_output ? 1U : 0U,
        snapshot.usb_output ? 1U : 0U);

    if (snapshot.has_axp_detail)
    {
        Serial.printf(" axp_acin=%u axp_vbus=%u acin_mv=%.0f acin_ma=%.1f vbus_mv=%.0f vbus_ma=%.1f bat_chg_ma=%.1f "
                      "bat_dis_ma=%.1f",
                      snapshot.axp_acin ? 1U : 0U,
                      snapshot.axp_vbus ? 1U : 0U,
                      static_cast<double>(snapshot.acin_mv),
                      static_cast<double>(snapshot.acin_ma),
                      static_cast<double>(snapshot.axp_vbus_mv),
                      static_cast<double>(snapshot.axp_vbus_ma),
                      static_cast<double>(snapshot.battery_chg_ma),
                      static_cast<double>(snapshot.battery_dis_ma));
    }

    Serial.println();
}

void draw_item(uint8_t index, const char* label, const char* value)
{
    const int16_t column_width = M5.Display.width() / kColumnCount;
    const int16_t line_height  = static_cast<int16_t>(M5.Display.fontHeight() + kLineGap);
    const int16_t column       = index % kColumnCount;
    const int16_t row          = index / kColumnCount;
    const int16_t x            = static_cast<int16_t>(kMarginX + column * column_width);
    const int16_t y            = static_cast<int16_t>(kMarginY + row * line_height);

    char text[32];
    snprintf(text, sizeof(text), "%s:%s", label, value);
    M5.Display.drawString(text, x, y);
}

void draw_item_u32(uint8_t& index, const char* label, uint32_t value)
{
    char value_text[16];
    snprintf(value_text, sizeof(value_text), "%lu", static_cast<unsigned long>(value));
    draw_item(index++, label, value_text);
}

void draw_item_i32(uint8_t& index, const char* label, int32_t value)
{
    char value_text[16];
    snprintf(value_text, sizeof(value_text), "%ld", static_cast<long>(value));
    draw_item(index++, label, value_text);
}

void draw_item_f1(uint8_t& index, const char* label, float value)
{
    char value_text[16];
    snprintf(value_text, sizeof(value_text), "%.1f", static_cast<double>(value));
    draw_item(index++, label, value_text);
}

void draw_item_bool(uint8_t& index, const char* label, bool value)
{
    draw_item(index++, label, value ? "1" : "0");
}

void draw_pmic_screen(const PmicSnapshot& snapshot)
{
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextFont(1);
    M5.Display.setTextSize(1.0f);
    M5.Display.setTextDatum(top_left);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);

    uint8_t item = 0;
    draw_item_u32(item, "ms", snapshot.now_ms);
    draw_item(item++, "chg", charging_short_name(snapshot.charging));
    draw_item_i32(item, "bat", snapshot.battery_pct);
    draw_item_i32(item, "bV", snapshot.battery_mv);
    draw_item_i32(item, "bI", snapshot.battery_ma);
    draw_item_i32(item, "VB", snapshot.vbus_mv);
    draw_item_bool(item, "ext", snapshot.ext_output);
    draw_item_bool(item, "usb", snapshot.usb_output);

    if (snapshot.has_axp_detail)
    {
        draw_item_bool(item, "AC", snapshot.axp_acin);
        draw_item_bool(item, "VBp", snapshot.axp_vbus);
        draw_item_f1(item, "ACV", snapshot.acin_mv);
        draw_item_f1(item, "ACI", snapshot.acin_ma);
        draw_item_f1(item, "VBV", snapshot.axp_vbus_mv);
        draw_item_f1(item, "VBI", snapshot.axp_vbus_ma);
        draw_item_f1(item, "BC+", snapshot.battery_chg_ma);
        draw_item_f1(item, "BC-", snapshot.battery_dis_ma);
    }
}

} // namespace

void test_pmic()
{
    Serial.begin(115200);
    delay(1000);

    auto cfg         = M5.config();
    cfg.internal_mic = false;
    cfg.internal_spk = false;
    M5.begin(cfg);

    M5.Display.setBrightness(255);
    M5.Display.setColorDepth(16);
    M5.Display.fillScreen(TFT_BLACK);

    Serial.println();
    Serial.printf("%s: starting PMIC dump\n", TAG);

    uint32_t last_report_ms = 0;

    while (true)
    {
        M5.update();

        const uint32_t now_ms = millis();
        if (last_report_ms == 0 || static_cast<uint32_t>(now_ms - last_report_ms) >= kReportPeriodMs)
        {
            const PmicSnapshot snapshot = read_pmic();
            print_pmic_line(snapshot);
            draw_pmic_screen(snapshot);
            last_report_ms = now_ms;
        }

        delay(1);
    }
}
