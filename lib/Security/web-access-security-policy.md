This document pertains to accessing the web server implemented internally to the ESP32

## Wi-Fi Requirements

All Wi-Fi access will need a password, no open networks allowed

Basic functions, such as downloading files from microSD card and uploading files to microSD card, can be done from any Wi-Fi network.

Functions that are administrative, reconfiguration, password resets, must happen on the default soft-AP.

The default soft-AP must enforce the use of WPA3 with only one user allowed, as the layer of authentication and encryption. This prevents impersonation and network sniffing.

The default soft-AP must use a randomly generated password. The password will be shown on the LCD screen.

Administrative actions such as reconfiguration can only happen with authentication that involves the user's password.

## Device Information

These items can be requested without authentication, and is sent without encryption:

 * Device Name
 * BDADDR
 * Wi-Fi MAC
 * Self IP
 * Firmware Version
 * Disk Storage

Other data included in this data package includes data for the session cryptographic functionalities.

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

Data is further encrypted using the session-key (on top of WPA3 security)

Authentication is included in the encryption

Data can contain identifiers that may need to be protected

## Configuration JSON Upload

Data is further encrypted using the session-key (on top of WPA3 security)

Authentication is included in the encryption

Data will contain SSID passwords, amongst other data

## Setting New Password

This only resets the network-key. The user is prompted for a new password, which gets transformed into a network-key on the front-end. The front-end sends it back to the ESP32's server.

This action must be allowed even if the user isn't authenticated.

Resetting this network-key triggers all NVS data to be erased. The filecrypt-key will be erased, and so, it will be automatically re-generated.

The transport of this happens over a WPA3 enforced Wi-Fi connection that only allows for one user and has a randomly generated password. This configuration will encrypt the transport and prevent sniffing and impersonation.
