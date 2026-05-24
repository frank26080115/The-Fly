This document pertains to accessing the web server implemented internally to the ESP32

## Wi-Fi Requirements

All Wi-Fi access will need a password, no open networks allowed

Administrative actions such as reconfiguration can only happen with authentication

Setting a new master-key will require the Wi-Fi to be using the default soft AP mode, forcing a single user connection, WPA3 security, and using a randomly generated password.

## Timestamp Authentication

The HTTP request carries a key-value pair named 'timestamp' and is formatted as 'YYYY-MM-DD-HH:mm:SS'

That string is also hashed with the master-key, and carried with the request under the key 'hash'

The server can then authenticate whether or not the time is recent (within +/- 2 minutes) and if the client knows the master-key

## Device Information

These items can be requested without authentication, and is sent without encryption:

 * Device Name
 * BDADDR
 * Wi-Fi MAC
 * Self IP
 * Firmware Version
 * Disk Storage

The current time and a cryptographic challenge is also delivered with this information.

## Time Sync

This is the only administrative action that is authenticated but not authenticated through using the time.

The server provides the cryptographic challenge, this is hashed with the master-key by the front-end to produce a response. The response and the browser time is delivered to the ESP32 in order to sync the RTC time.

Performing time-sync both invalidates the session-challenge in the back-end, and also causes a page refresh.

## File Upload

Requires no authentication

File name is specified in plain-text

File is uploaded in plain-text

I understand this is insecure, this does not expose keys. This makes it easy for people to automate local network workflows.

## File List Download

Requires no authentication

Data is transported in plain-text

I understand this is insecure, this does not expose keys. This makes it easy for people to automate local network workflows.

## File Download

Requires no authentication

Data is transported in plain-text but the recording files are encrypted themselves, any other file will be in plain-text.

I understand this is insecure, this does not expose keys. This makes it easy for people to automate local network workflows. A master-key is still required to decrypt any of the files.

## Configuration JSON Download

Requires timestamp-authentication

Data is encrypted using the master-key

Data can contain identifiers that may need to be protected

## Configuration JSON Upload

Requires timestamp-authentication

Data is encrypted using the master-key

Data will contain SSID passwords, amongst other data

## Setting New Master-Key

A temporary short key is shown to the user via LCD display

The user types this into the web browser along with the new password, and attempts to submit it.

The temporary short key is validated to prevent typos, if validated, a longer temporary key is generated. The new master-key is generated. The new master-key is encrypted with the temporary long key, and then transmitted.

This action must be allowed even if the user isn't authenticated. The firmware will erase all data within NVS when a new master-key is set.

Note that the server cannot really authenticate anything about the new master-key, and so, this simple action can cause NVS to be erased.

A denial-of-service attack here can potentially annoy the owner of the device, causing files to be encrypted with an unknown key, and deleting all Bluetooth and Wi-Fi connections.
