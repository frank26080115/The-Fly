This is a currently a road-map, not everything is implemented yet

# Goals

A balance between convenience and security.

The device should be quick to use. A pin entry screen is considered but not mandatory. The microSD card is used without any special software.

The device can be lost, which is the most likely threat to encounter, and that doesn't compromise data, and the user can quickly take action to unpair the device from Bluetooth hosts.

The uploading and transcription of the recorded audio files should be nearly seamless without user interaction. The downloading of final results is outside the scope of this project.

# Major Zones

 * Device (the ESP32 device)
 * Cloud Server
 * Local Network PC
 * User Web Browser (front-end)

# Transports

 * Wi-Fi is used but assume WPA2 or better
 * HTTP is used by the ESP32's web server, HTTPS is impractical in this context, message-layer-encryption is used in some cases
 * HTTPS is used between the ESP32 and any server
 * The ESP32 uses an external flash memory IC over SPI bus, the data here is encrypted

# Secrets

 * password
 * master-key
 * link-key
 * Wi-Fi credentials
 * session identifier
 * temporary short key
 * temporary long key
 * session challenge
 * session response
 * the recording files
 * audio/sound

# Identities

 * Bluetooth Device Address, aka BDADDR
 * Bluetooth Names
 * Wi-Fi MAC Address
 * SSIDs

All of these are easily seen or captured, and are not considered secure or require security when transported.

Spoofing SSID or Bluetooth name is considered a social engineering attack and is protected against with underlying security mechanisms.

Spoofing of the BDADDR is not useful without a correct link-key

# Threats

 * finding exploits in the open source firmware
 * stolen device
 * data traffic interception
 * looking at the device while a temporary key is shown
 * listening to the sound

## Audio/Sound

The device, in its current implementation, is a glorified speakerphone. Please be mindful of where you are using it and who is listening.

The audio data on the circuit board is communicated via I2S or raw analog signals, no encryption is possible.

## Device

This ESP32 has RAM memory, a SPI flash used for NVS and LittleFS, and e-fuses. There is also a RTC that is used for some security functions by simply providing the correct current time.

As a policy, RAM is considered safe. RAM does not need to be encrypted.

NVS is encrypted and the handling of this is mostly fully transparent, the key for this lives in the e-fuse. The key is random when generated and can never be read. As a policy, the NVS is considered safe from attacks such as dumping the flash memory IC itself.

There is a copy of the master-key stored in NVS.

The contents of the NVS is not further encrypted by the running firmware. But changing the master-key will trigger NVS to be erased.

The microSD card does not use full-disk-encryption. All `*.rec` files are encrypted, but the timestamps are exposed. `cloud_history.txt` is also not encrypted. If a firmware update file is present, it is not encrypted, it contains no secrets, and the firmware file is probably signed.

The local device runs a web server, which can be used to administer the device (change settings, change passwords) and to manage files. All the HTTP requests to this server needs to be authenticated through methods involving request parameters. For transmitting secrets, such as Wi-Fi credentials, explicit encryption is involved in the transport. For file downloads, the file is assumed to be already encrypted. For file uploads, the file is not protected, as the only uploads expected are either a cleaned up history file or a firmware update. HTTPS is not practical to implement. SFTP is impractical to implement.

The local device can use mDNS and service discovery and other broadcast methods to signal to a local network that it is available. This allows for easier automated workflows on local networks.

When a local soft-AP is used, it only allows for one client, and enforces WPA3 PSK security. The password is randomize each time as one of the measures to prevent an impersonation phishing attack. MAC address verification is not strong enough for phishing prevention, as it can be spoofed.

The act of resetting the master-key can only be performed while using the default soft AP.

The Bluetooth functionality of the local device is always in a connectable-but-not-discoverable state, unless BT pairing mode is active.

External interfaces to the ESP32 will be locked down such that security is not compromised. Firmware updates must be authenticated using Secure Boot v2.

If Stolen: all files encrypted; all NVS encrypted; no key can be leaked; attacker can connect to bonded Bluetooth hosts; attacker can see timestamps of files and uploads

If Stolen Recommended Action: unpair the old device from all Bluetooth hosts.

## Cloud Server

The cloud server is a privately held virtual private server. It relies on its own security (such as user authentication, disk encryption, etc).

The master-key will need to be stored on it. If the server is compromised, the master-key is considered compromised.

The software should use a key-file, and if it is missing, ask the user for a password as a first time setup procedure.

There will be unencrypted "*.wav" files stored on the server, and they will need to be transcribed by either a local AI or a cloud AI service. The transcription will need to be further analyzed by either a local AI or cloud AI service.

Uploads are authenticated via request header data. Unauthenticated uploads are not stored. This prevents both denial-of-service attacks and also gaslighting.

The server implementation present in this repository is considered only demonstrative. It is up to the user how the server is setup in terms of user access and networking. It is up to the user how the unencrypted data is stored. The user may want to stream unencrypted data instead of ever letting it be saved to disk. This is outside the scope of this project.

If Compromised: if the attacker actually gets the login credentials, this is bad, it's a complete compromise of the master-key and all encrypted files

If Spoofed: the spoof server can get a copy of the file but will not be able to decrypt it; the spoof server can get samples of encrypted timestamps to attempt cryptanalysis with

## Local Network PC

This is the same as a cloud server but is in a local network with the device, with the same security implications.

The software should use a key-file, and if it is missing, ask the user for a password as a first time setup procedure.

There is extra functionality available while in a local network with the device, such as the possibility of implementing automated workflows by issuing HTTP requests.

There is no ability to automate functionality involving the temporary short key (and thus, the derived temporary long key), as these are randomized per-session and require the human to input.

If Compromised: if the attacker actually gets the login credentials, this is bad, it's a complete compromise of the master-key and all encrypted files

## User Web Browser

Similar to a Local Network PC, but there is no permanent storage for a master-key. Any functionality involving the master-key will need to require the human to input the password. The password is never transmitted in any form. Any secret is never transmitted in plain-text.

Transmission of secrets will use temporary keys to assist in the encrypted transport.

If Compromised: if there is malware that can peak at the session memory, then there is the chance of compromising some keys when they are in memory

## Password

This means a string that the user is able to type and remember in their head. It is never stored or transmitted (neither encrypted nor plain-text)

If Stolen: attacker can generate the Master-Key

## Master-Key

32 byte array, derived from the password. Used for most security functions. Saved on servers (remote, cloud, and local).

The user can choose to reset the master-key on the device at any time, but doing so will cause NVS to be erased. The new master-key is to be transmitted while encrypted by a temporary key.

If Stolen: attacker can decrypt all files; attacker can authenticate to servers; attacker can derive other keys; attacker cannot extract Bluetooth link-keys

## Link-Key

Link-Keys are specific to each Bluetooth host pairing, this is handled by the ESP-IDF's internal Bluetooth stack, and is stored in NVS.

The firmware will handle the deletion of these keys when the user requests it. The stack handles the creation of these keys.

These keys never need to be exposed beyond internally to the Bluetooth stack. The application firmware itself, and beyond, never needs to read it.

If Stolen: attacker can intercept Bluetooth audio, and impersonate the device

## Wi-Fi Credientials

The SSID and passwords of Wi-Fi stations are stored in NVS memory.

These can be set by the web browser front-end, and when doing so, it is transported while encrypted.

Wi-Fi SSIDs are transmitted from internal server to browser but not the passwords. SSID passwords are considered write only and never read out by the front-end.

If Stolen: attacker can connect to the Wi-Fi network

## Session Identifier

A random 32 bit integer generated once per page-load of the ESP32's web server. It is only used in the context of the ESP32's web server communicating with a directly connected web browser, not cloud service. It is transmitted in plain-text to and from the web browser. It's not a secret and its only purpose is to make sure that the temporary key used is still valid. If either side detects a change in this session identifier, it knows that the previous temporary key is invalid.

If Stolen: nothing

## Session Challenge and Session Response

This is only used for time sync between web browser and the ESP32's web server.

The session-challenge and session-response shall both be a 32 byte array. The session-challenge is randomly generated and is not tied to anything.

The session-response is generated by hashing the session-challenge with the master-key.

When the front-end sends back a time to sync against, the session-response is also attached. The server will verify the session-response is correct before accepting the time-sync.

Performing time-sync both invalidates the session-challenge in the back-end, and also causes a page refresh.

If Stolen: nothing

## Temporary Keys

The temporary short key is randomly generated 8 character code. It is not tied in any way to the session identifier, nor the master-key. The short key is random but also self-validating, so that the front-end can verify the user has input it correctly.

It is never transmitted in any electronic or radio form, but it is displayed on the LCD screen of the device. The user must never let somebody see it. But it does not need to be remembered beyond one usage session. The GUI will make it convenient to hide this number from view, and implement a time limit for when this number is in view.

The temp-long-key is derived from the temp-short-key, and is used to encrypt messages between the ESP32's web server and a directly connected web browser.

The temp-long-key is a 32 byte array. It is never transmitted in any form.

If Stolen (seen by eye): attacker can potentially capture a new master-key if the user is setting up a new master-key, through a phishing attack or monitoring HTTP traffic

## Encryption and Hask Functions

AES-GCM for file encryption.

HMAC-SHA-256 for authentication to servers.

PBKDF2-HMAC-SHA-256 for key generation.

## File Encryption

The `*.rec` files that are recorded have fixed size packets. Each packet is encrypted entirely, using the master-key.

The packet header has some static features like a magic and source identifier, some parts that are dynamic like a timestamp and sequence number, plus 64 bits of random nonce.

## Authentication Over HTTP/HTTPS Request Headers

For all requests, the current time (UTC) is to be included, and a hash of that time (with the master-key) is to be included. The server can verify that the time is in-sync within +/- 2 minutes, and infer that the master-key is correct.

It is critical that the time is correct on all parties involved.

## Front-End Authentication

The user will type in their password, this gets hashed into the master-key, but neither of these are transmitted.

The displayed page will be void of any data at this point. The fields on the page are all populated via Ajax after authentication completes.

The ESP32 backend will issue a challenge, the front-end uses the master-key to generate a crypto-response.

The front-end uses the crypto-response to issue a Ajax request to fetch the data required to populate the front-end page. This is the only time the crypto-response is used, as it can be easily captured.

The challenge and crypto-response are both 32 bit arrays. The challenge is randomly generated once per page load.

If either is stolen: the master-key is still safe; attacker can view file lists and SSID names and BT host information.

## Touch Screen Pin Login

Under consideration

If implemented...

A 6 digit pin input is requested on boot

3 quick attempts allowed, after that it will have a cooldown to prevent brute forcing

The pin is derived from the master-key, it can be shown on the web administrative page, it cannot be changed. To change it, the master-key must be changed, and thus, forcing the refresh of all other parts of the security chain

If Stolen: master-key is not compromised; attacker can connect to any bonded BT host; attacker can connect to Wi-Fi stations in memory; no keys are exposed directly; Wi-Fi passwords can be compromised by analyzing the Wi-Fi connections

Implementation details are not yet fully established.

## Firmware Update

Only authenticated firmware files can be used for updating. This prevents an attacker from updating to a firmware that simply dumps out the NVS data.

The update mechanism also checks to see if the firmware version is equal or newer than the currently running firmware, this prevents attackers downgrading to firmware with known vulnerabilities.

The firmware does not need to be encrypted.

The firmware file will be publicly available to download, and loaded on to the device's memory (either microSD card or an dedicated OTA staging memory). While on the device, the currently running firmware can run checks on it before passing it on to the bootloading mechanism, which will have more security checks on it before finally flashing.

See `lib\Security\firmware-authentication.md` for more details.

Implementation details are not yet fully established.

## Random Number Generation

`esp_random()` and `esp_fill_random()` shall be used, never use `rand()`. The `esp_` RNG functions are using hardware RNG which works better if Bluetooth or Wi-Fi is currently enabled. It is policy that random numbers are only generated if the wireless subsystems are active. By current design, Bluetooth is always on and connectable with very rare exceptions, and Wi-Fi might shutdown Bluetooth but the time window when both are off will not be used for RNG.
