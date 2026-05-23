The goal is to use Secure Boot V2 for firmware updates

https://docs.espressif.com/projects/esp-idf/en/stable/esp32/security/secure-boot-v2.html

# Goals

Prevent rough firmware from running, which can extract keys from NVS, which is the primary concern.

Prevent older firmware from being run on the device, older firmware which may contain security vulnerabilities.

WARNING: These security measures does not prevent somebody from running unsigned or older firmware on blank untouched ESP32 devices. The primary goal of protecting the secret keys is still met.

# Workflow

 * microSD/LittleFs stores firmware.bin
 * running app copies it into inactive OTA partition
 * esp_ota_end() verifies image/signature
 * if successfully verified
   * use esp_ota_set_boot_partition()
   * reboot
   * the bootloader will verify on every boot
 * if unsuccessful
   * do NOT call esp_ota_set_boot_partition()
   * delete/ignore partial OTA image
   * show user error via GUI
