There is a 16MB on-board flash and also a microSD card slot connected via SPI

The application code will treat both as separate disks

LittleFS will be used

There will be a FTP server implemented, if possible, both disks are served as root directories, one being `on-board` and the other being `microSD`. If the FTP server implementation can only use one, then only `microSD` will be served.

The microSD card will contain JSON files that are used to configure Bluetooth hosts and Wi-Fi connections
