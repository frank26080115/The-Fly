#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include "esp_wifi.h"

#include "AudioManager.h"

namespace
{

constexpr const char* TAG = "test_batteryburn";

constexpr uint32_t kDisplayUpdatePeriodMs = 250;
constexpr uint32_t kWifiBurstPeriodMs     = 20;
constexpr uint16_t kWifiUdpPort           = 48000;
constexpr uint8_t  kWifiChannel           = 6;
constexpr int8_t   kWifiTxPowerQdbm       = 78; // 19.5 dBm in quarter-dBm units.
constexpr uint8_t  kHapticLevel           = 255;
constexpr size_t   kI2sSilenceChunk       = 512;
constexpr size_t   kWifiPacketsPerBurst   = 4;
constexpr size_t   kWifiPayloadBytes      = 1200;

constexpr uint32_t kI2sBurnRatesHz[] = {
    192000,
    96000,
    48000,
};

WiFiUDP  g_udp;
uint8_t  g_wifi_payload[kWifiPayloadBytes];
uint32_t g_start_ms              = 0;
uint32_t g_next_display_update_ms = 0;
uint32_t g_next_wifi_burst_ms     = 0;
uint32_t g_i2s_rate_hz            = 0;
bool     g_haptic_enabled         = true;
bool     g_wifi_started           = false;
bool     g_i2s_started            = false;

void setup_display()
{
    M5.Display.setBrightness(255);
    M5.Display.setColorDepth(16);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}

void setup_buttons()
{
    M5.BtnA.setDebounceThresh(20);
    M5.BtnC.setDebounceThresh(20);
}

void format_elapsed(uint32_t elapsed_s, char* out, size_t out_size)
{
    const uint32_t hours   = elapsed_s / 3600U;
    const uint32_t minutes = (elapsed_s / 60U) % 60U;
    const uint32_t seconds = elapsed_s % 60U;
    snprintf(out,
             out_size,
             "%02lu:%02lu:%02lu",
             static_cast<unsigned long>(hours),
             static_cast<unsigned long>(minutes),
             static_cast<unsigned long>(seconds));
}

void draw_status()
{
    const int32_t battery_mv = M5.Power.getBatteryVoltage();
    const uint32_t elapsed_s = (millis() - g_start_ms) / 1000U;

    char voltage_text[24];
    char runtime_text[24];
    snprintf(voltage_text, sizeof(voltage_text), "%.2f V", static_cast<double>(battery_mv) / 1000.0);
    format_elapsed(elapsed_s, runtime_text, sizeof(runtime_text));

    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextFont(7);
    M5.Display.setTextSize(1.0f);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.drawString(voltage_text, M5.Display.width() / 2, 78);
    M5.Display.drawString(runtime_text, M5.Display.width() / 2, 160);
}

void apply_haptic()
{
    M5.Power.setVibration(g_haptic_enabled ? kHapticLevel : 0);
}

void handle_buttons()
{
    M5.update();

    if (M5.BtnA.wasPressed())
    {
        g_haptic_enabled = true;
        apply_haptic();
        Serial.printf("%s: haptic on\n", TAG);
    }
    if (M5.BtnC.wasPressed())
    {
        g_haptic_enabled = false;
        apply_haptic();
        Serial.printf("%s: haptic off\n", TAG);
    }
}

bool start_i2s_burn()
{
    if (!AudioManager::init(AudioManager::Hardware::M5StackInternal))
    {
        Serial.printf("%s: AudioManager init failed\n", TAG);
        return false;
    }

    for (uint32_t rate_hz : kI2sBurnRatesHz)
    {
        if (AudioManager::enableSpeakerMode(rate_hz))
        {
            g_i2s_rate_hz = rate_hz;
            AudioManager::setVolume(AudioManager::kMaxVolume);
            AudioManager::bluetoothToSpeakerFifo().clear();
            AudioManager::bluetoothToSpeakerFifo().setSilenceWhenEmpty(true);
            Serial.printf("%s: I2S burn clock started at %lu Hz\n", TAG, static_cast<unsigned long>(rate_hz));
            return true;
        }
        Serial.printf("%s: I2S burn clock failed at %lu Hz\n", TAG, static_cast<unsigned long>(rate_hz));
    }

    return false;
}

void feed_i2s()
{
    AudioFifo& fifo = AudioManager::bluetoothToSpeakerFifo();
    while (fifo.availableToWrite() >= kI2sSilenceChunk)
    {
        fifo.queueSilence(kI2sSilenceChunk);
    }
    AudioManager::pump_bt2spk();
}

void fill_wifi_payload()
{
    uint32_t value = 0x12345678U;
    for (size_t i = 0; i < sizeof(g_wifi_payload); ++i)
    {
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        g_wifi_payload[i] = static_cast<uint8_t>(value & 0xFFU);
    }
}

bool start_wifi_burn()
{
    fill_wifi_payload();

    WiFi.mode(WIFI_AP);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(kWifiTxPowerQdbm);

    const bool started = WiFi.softAP("thefly-battery-burn", nullptr, kWifiChannel, false, 4);
    if (!started)
    {
        Serial.printf("%s: Wi-Fi AP start failed\n", TAG);
        return false;
    }

    g_udp.begin(kWifiUdpPort);
    Serial.printf("%s: Wi-Fi AP started ssid=thefly-battery-burn ip=%s channel=%u\n",
                  TAG,
                  WiFi.softAPIP().toString().c_str(),
                  static_cast<unsigned>(kWifiChannel));
    return true;
}

void send_wifi_burst()
{
    if (!g_wifi_started)
    {
        return;
    }

    for (size_t i = 0; i < kWifiPacketsPerBurst; ++i)
    {
        g_udp.beginPacket(IPAddress(255, 255, 255, 255), kWifiUdpPort);
        g_udp.write(g_wifi_payload, sizeof(g_wifi_payload));
        g_udp.endPacket();
    }
}

} // namespace

void test_batteryburn()
{
    Serial.begin(115200, SERIAL_8N1, -1, 1);
    delay(250);

    setup_display();
    setup_buttons();

    Serial.println();
    Serial.printf("%s: starting battery burn test\n", TAG);
    Serial.printf("%s: BtnA=haptic on BtnC=haptic off\n", TAG);

    g_start_ms = millis();
    apply_haptic();

    g_i2s_started  = start_i2s_burn();
    g_wifi_started = start_wifi_burn();

    Serial.printf("%s: burn started i2s=%u i2s_rate=%lu wifi=%u haptic=%u\n",
                  TAG,
                  g_i2s_started ? 1U : 0U,
                  static_cast<unsigned long>(g_i2s_rate_hz),
                  g_wifi_started ? 1U : 0U,
                  g_haptic_enabled ? 1U : 0U);

    while (true)
    {
        handle_buttons();

        const uint32_t now_ms = millis();
        if (static_cast<int32_t>(now_ms - g_next_display_update_ms) >= 0)
        {
            draw_status();
            g_next_display_update_ms = now_ms + kDisplayUpdatePeriodMs;
        }
        if (static_cast<int32_t>(now_ms - g_next_wifi_burst_ms) >= 0)
        {
            send_wifi_burst();
            g_next_wifi_burst_ms = now_ms + kWifiBurstPeriodMs;
        }

        if (g_i2s_started)
        {
            feed_i2s();
        }
        apply_haptic();
        delay(1);
    }
}
