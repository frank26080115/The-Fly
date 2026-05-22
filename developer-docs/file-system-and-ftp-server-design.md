There is a 16MB on-board flash and also a microSD card slot connected via SPI

The microSD card shall be FAT32.

The microSD card shall be considered a permanent part of the circuitry. Assume it is initialized once during boot, and any errors associated with the card is then treated as a fatal error requiring a reboot. There is no graceful recovery for ejecting the card while the device is running.
