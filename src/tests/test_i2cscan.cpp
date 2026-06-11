#include <Arduino.h>
#include <M5GFX.h>

#include "driver/i2c.h"
#include "pins.h"
#include "utilfuncs.h"
#include "AudioManager.h"

namespace
{

constexpr const char* TAG             = "test_i2cscan";
constexpr uint32_t    kI2cScanFreq    = 400000;
constexpr i2c_port_t  kInternalI2cPort = I2C_NUM_1;
constexpr uint8_t     kFirstAddress   = 0x08;
constexpr uint8_t     kLastAddress    = 0x77;
constexpr uint8_t     kMaxAddressHits = kLastAddress - kFirstAddress + 1;

struct I2cScanResult
{
    uint32_t now_ms                       = 0;
    bool     mclk_started                 = false;
    bool     bus_init_ok                  = false;
    uint8_t  addresses[kMaxAddressHits]   = {};
    bool     write_ack[kMaxAddressHits]   = {};
    bool     read_ack[kMaxAddressHits]    = {};
    uint8_t  address_count                = 0;
};

bool probe_address_write(uint8_t address)
{
    const auto started = m5gfx::i2c::beginTransaction(kInternalI2cPort, address, kI2cScanFreq, false);
    if (started.has_error())
    {
        return false;
    }

    return m5gfx::i2c::endTransaction(kInternalI2cPort).has_value();
}

bool probe_address_read(uint8_t address)
{
    uint8_t value = 0;
    return m5gfx::i2c::transactionRead(kInternalI2cPort, address, &value, 1, kI2cScanFreq).has_value();
}

I2cScanResult scan_internal_i2c()
{
    I2cScanResult result;
    result.now_ms      = millis();
    result.bus_init_ok = m5gfx::i2c::init(kInternalI2cPort, kInternalI2cSda, kInternalI2cScl).has_value();

    if (!result.bus_init_ok)
    {
        return result;
    }

    for (uint8_t address = kFirstAddress; address <= kLastAddress; ++address)
    {
        const bool write_ack = probe_address_write(address);
        const bool read_ack  = probe_address_read(address);
        if (write_ack || read_ack)
        {
            const uint8_t index          = result.address_count++;
            result.addresses[index]      = address;
            result.write_ack[index]      = write_ack;
            result.read_ack[index]       = read_ack;
        }
    }

    return result;
}

void print_scan_result(const I2cScanResult& result)
{
    if (!result.bus_init_ok)
    {
        Serial.printf("%s: ms=%lu mclk=%s bus_init=failed\n",
                      TAG,
                      static_cast<unsigned long>(result.now_ms),
                      result.mclk_started ? "started" : "failed");
        return;
    }

    Serial.printf("%s: ms=%lu mclk=%s freq=%lu count=%u addresses=",
                  TAG,
                  static_cast<unsigned long>(result.now_ms),
                  result.mclk_started ? "started" : "failed",
                  static_cast<unsigned long>(kI2cScanFreq),
                  static_cast<unsigned>(result.address_count));

    if (result.address_count == 0)
    {
        Serial.print("none");
    }
    else
    {
        for (uint8_t i = 0; i < result.address_count; ++i)
        {
            Serial.printf("%s0x%02X(%c%c)",
                          i == 0 ? "" : " ",
                          result.addresses[i],
                          result.write_ack[i] ? 'W' : '-',
                          result.read_ack[i] ? 'R' : '-');
        }
    }

    Serial.println();
}

} // namespace

void test_i2cscan()
{
    Serial.begin(115200, SERIAL_8N1, -1, 1);

    delay(250);

    Serial.println();
    Serial.printf("%s: starting one-shot internal I2C scan with SGTL5000 MCLK enabled freq=%lu\n",
                  TAG,
                  static_cast<unsigned long>(kI2cScanFreq));
    Serial.printf("%s: using m5gfx::i2c directly port=%d sda=%d scl=%d addr_range=0x%02X..0x%02X\n",
                  TAG,
                  static_cast<int>(kInternalI2cPort),
                  kInternalI2cSda,
                  kInternalI2cScl,
                  kFirstAddress,
                  kLastAddress);
    Serial.printf("%s: enabling shared I2S MCLK on GPIO%d; UART TX is disconnected during the scan\n",
                  TAG,
                  kSGTL5000I2sMclk);
    Serial.flush();

    const bool mclk_started = AudioManager::configure_i2s_shared();
    delay(250);

    I2cScanResult result = scan_internal_i2c();
    result.mclk_started  = mclk_started;

    AudioManager::stop();
    AudioManager::reconnect_uart0_tx();
    delay(250);

    Serial.println();
    print_scan_result(result);
    Serial.printf("%s: scan complete\n", TAG);
    Serial.flush();

    idle_forever();
}
