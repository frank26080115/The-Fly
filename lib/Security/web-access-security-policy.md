This document pertains to accessing the web server implemented internally to the ESP32

## Wi-Fi Requirements

All Wi-Fi access will need a password, no open networks allowed

Administrative actions such as reconfiguration can only happen with authentication

Setting a new master-key-pair will require the Wi-Fi to be using the default soft AP mode, forcing a single user connection, WPA3 security, and using a randomly generated password.

## Device Information

These items can be requested without authentication, and is sent without encryption:

 * Device Name
 * BDADDR
 * Wi-Fi MAC
 * Self IP
 * Firmware Version
 * Disk Storage

The current time and a cryptographic session challenge is also delivered with this information.

## Time Sync

Time sync can be performed at any time when authenticated, and also automatically during password reset.

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

Data is encrypted using the session-key

Authentication is included in the encryption

Data can contain identifiers that may need to be protected

## Configuration JSON Upload

Data is encrypted using the session-key

Authentication is included in the encryption

Data will contain SSID passwords, amongst other data

## Setting New Password

A temporary short key is shown to the user via LCD display

The user types this into the web browser along with the new password, and attempts to submit it.

The temporary short key is validated to prevent typos, if validated, a longer temporary key is generated. The new master-key-pair are generated. The 2 new keys are encrypted with the temporary long key, and then transmitted. The transmission also includes the session-challenge as a way of verifying that the temporary key used is still valid. The time is also transmitted as a way to perform a first-time-setup time-sync.

This action must be allowed even if the user isn't authenticated. The firmware will erase all data within NVS when a new master-key-pair is set.

Note that the server cannot really authenticate anything about the new master-key-pair, and so, this simple action can cause NVS to be erased.

A denial-of-service attack here can potentially annoy the owner of the device, causing files to be encrypted with an unknown key, and deleting all Bluetooth and Wi-Fi connections.
