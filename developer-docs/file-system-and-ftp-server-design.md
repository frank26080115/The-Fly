There is a 16MB on-board flash and also a microSD card slot connected via SPI

The application code will treat both as separate disks

LittleFS will be used

There will be a FTP server implemented, if possible, both disks are served as root directories, one being `on-board` and the other being `microSD`. If the FTP server implementation can only use one, then only `microSD` will be served.

The microSD card will contain JSON files that are used to configure Bluetooth hosts and Wi-Fi connections

Files named like `bthost_x.json` where `x` is a number will contain data about a particular Bluetooth host:
 * name (what is shown in the UI)
 * MAC address
 * no need to store link key as ESP-IDF stores this internally

Files named like `wifi_x.json` will contain Wi-Fi SSIDs and passwords to be connected to, such as a home router. This JSON will also contain a timezone string so that NTP can be used to set the RTC time.

A file named `wifiap.json` will contain information needed to start Wi-Fi as an access point, such as the SSID and a password.
