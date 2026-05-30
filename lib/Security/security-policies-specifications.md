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
 * Wi-Fi WPA3, and a single-user-allowed policy, is enforced when data needs to be securely transmitted
 * HTTP is used by the ESP32's web server, HTTPS is impractical in this context, message-layer-encryption is used in some cases
 * HTTPS is used between the ESP32 and any server
 * The ESP32 uses an external flash memory IC over SPI bus, the data here is encrypted

# Potential Secrets

 * password
 * filecrypt-key
 * network-key
 * link-key
 * Wi-Fi credentials
 * enrollment confirmation code
 * session authentication and encryption
 * tamper evidence code
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
 * looking at the device while a key or password is shown on the screen
 * listening to the sound
 * malware on servers or browsers

## Encryption and Hash Functions

AES-GCM for file encryption.

HMAC-SHA-256 for authentication to servers.

PBKDF2-HMAC-SHA-256 for key generation. 32 byte salt, 10000 iterations.

## Audio/Sound

The device, in its current implementation, is a glorified speakerphone. Please be mindful of where you are using it and who is listening.

The audio data on the circuit board is communicated via I2S or raw analog signals, no encryption is possible.

## Device

This ESP32 has RAM memory, a SPI flash used for NVS and LittleFS, and e-fuses. There is also a RTC that is used for some security functions by simply providing the correct current time.

As a policy, RAM is considered safe. RAM does not need to be encrypted. The MPU will be configured to disallow code execution from RAM regions meant for data.

NVS is encrypted and the handling of this is mostly fully transparent, the key for this lives in the e-fuse. The key is random when generated and can never be read. As a policy, the NVS is considered safe from attacks such as dumping the flash memory IC itself.

There is a copy of the filecrypt-key and the network-key stored in NVS.

The contents of the NVS is not further encrypted by the running firmware. But changing network-key will trigger NVS to be erased.

The microSD card does not use full-disk-encryption. Security-level-0 `.wav` recordings are not encrypted. All `*.rec` files are encrypted, but the timestamps are exposed. `cloud_history.txt` is also not encrypted. If a firmware update file is present, it is not encrypted, it contains no secrets, and the firmware file is probably signed.

The local device runs a web server, which can be used to administer the device (change settings, change passwords) and to manage files. All the HTTP requests to this server needs to be authenticated through methods involving request parameters. For transmitting secrets, such as Wi-Fi credentials, explicit encryption is involved in the transport. For file downloads, the file is assumed to be already encrypted. For file uploads, the file is not protected, as the only uploads expected are either a cleaned up history file or a firmware update. HTTPS is not practical to implement. SFTP is impractical to implement.

The local device can use mDNS and service discovery and other broadcast methods to signal to a local network that it is available. This allows for easier automated workflows on local networks.

When a local soft-AP is used, it only allows for one client, and enforces WPA3 PSK security. The password is randomize each time as one of the measures to prevent an impersonation phishing attack. MAC address verification is not strong enough for phishing prevention, as it can be spoofed.

The act of resetting the filecrypt-key or network-key can only be performed while using the default soft AP.

The Bluetooth functionality of the local device is always in a connectable-but-not-discoverable state, unless BT pairing mode is active.

External interfaces to the ESP32 will be locked down such that security is not compromised. Firmware updates must be authenticated using Secure Boot v2.

If Stolen: all files encrypted; all NVS encrypted; no key can be leaked; attacker can connect to bonded Bluetooth hosts; attacker can see timestamps of files and uploads

If Stolen Recommended Action: unpair the old device from all Bluetooth hosts.

## Cloud Server

The cloud server is a privately held virtual private server. It relies on its own security (such as user authentication, disk encryption, etc).

The filecrypt-key and network-key will need to be stored on it. If the server is compromised, the filecrypt-key and network-key are both considered compromised.

The software should use a key-file, and if it is missing, ask the user for a password as a first time setup procedure.

There will be unencrypted "*.wav" files stored on the server, and they will need to be transcribed by either a local AI or a cloud AI service. The transcription will need to be further analyzed by either a local AI or cloud AI service.

Uploads are authenticated via request header data. Unauthenticated uploads are not stored. This prevents both denial-of-service attacks and also gaslighting.

The server implementation present in this repository is considered only demonstrative. It is up to the user how the server is setup in terms of user access and networking. It is up to the user how the unencrypted data is stored. The user may want to stream unencrypted data instead of ever letting it be saved to disk. This is outside the scope of this project.

If Compromised: if the attacker actually gets the login credentials, this is bad, it's a complete compromise of the filecrypt-key and network-key and all encrypted files

If Spoofed: the spoof server can get a copy of the file but will not be able to decrypt it; the spoof server can get samples of encrypted timestamps to attempt cryptanalysis with

## Local Network PC

This is the same as a cloud server but is in a local network with the device, with the same security implications.

The software should use a key-file, and if it is missing, ask the user for a password as a first time setup procedure.

There is extra functionality available while in a local network with the device, such as the possibility of implementing automated workflows by issuing HTTP requests.

If Compromised: if the attacker actually gets the login credentials, this is bad, it's a complete compromise of the filecrypt-key and network-key and all encrypted files

## User Web Browser

Similar to a Local Network PC, but there is no permanent storage. Any functionality involving the network-key will need to require the human to input the password. The password is never transmitted in any form. Any secret is never transmitted in plain-text. The filecrypt-key is not used in any web front-end functionality.

If Compromised: if there is malware that can peak at the session memory, then there is the chance of compromising some keys, or even the password, when they are in memory

## Password

This means a string that the user is able to type and remember in their head. It is never stored or transmitted (neither encrypted nor plain-text)

It is only used to generate a network-key

If Stolen: attacker can generate the network-key

## Master-Key-Pair

This simply refers to both the filecrypt-key and network-key. But they are never really used together. They simply exist together sometimes.

## Filecrypt-Key

32 byte array, randomly generated. Used for file encryption only. Saved on servers (remote, cloud, and local).

During enrollment, this is transmitted to a server in plain-text. Enrollment must only happen over HTTPS. Enrollment is the only time this key leaves the device, ever, literally.

The filecrypt-key is regenerated upon any change in cloud server configuration. This prevents a change in configuration to send an old filecrypt-key to a different malicious server.

If Stolen: attacker can decrypt all files that uses that key

## Network-Key

32 byte array, derived from the password. Used for all networking functionality.

Used for most security functions except file encryption. Saved on servers (remote, cloud, and local).

During enrollment, this is transmitted to a server in plain-text. Enrollment must only happen over HTTPS.

During password reset, this is transmitted from a web browser to the ESP32 in plain-text. This cannot happen over HTTPS because a CA trusted SSL server cannot be practically implemented on this embedded platform. This happens over a single user WPA3 Wi-Fi network that uses a randomized password.

If Stolen: attacker can authenticate to servers; attacker can view SSID passwords in transit; attacker cannot extract Bluetooth link-keys; attacker cannot derive password; attacker cannot derive filecrypt-key; attacker cannot decrypt files

## Enrollment Confirmation Code

The enrollment confirmation code is a short code derived from the combination of the filecrypt-key and network-key and the device MAC.

The enrollment confirmation code is shown on the ESP32's LCD screen.

The enrollment confirmation code is sent over to the cloud server as a part of enrollment.

The enrollment confirmation code is included in plain-text in every `*.rec` file.

The server will cache all the details about enrollment, and the user has to verify that the enrollment confirmation code on the server matches what the LCD screen shows, and then confirm the enrollment. NOTE: the server needs to only allow authenticated users to do this, and in this context, this authentication is outside the scope of this project.

It is considered not possible for the enrollment details (the master-key-pair and the enrollment confirmation code) to be sent to the wrong destination, as HTTPS is being used to prevent interception and impersonation.

But it is possible for somebody to flood a server with new enrollments, valid or not, which can degrade service. But this does not compromise the master-key-pair, and thus, encrypted files cannot be decrypted by an attacker.

If Stolen: the confirmation code itself does not compromise any keys

## Link-Key

Link-Keys are specific to each Bluetooth host pairing, this is handled by the ESP-IDF's internal Bluetooth stack, and is stored in NVS.

The firmware will handle the deletion of these keys when the user requests it. The stack handles the creation of these keys.

These keys never need to be exposed beyond internally to the Bluetooth stack. The application firmware itself, and beyond, never needs to read it.

If Stolen: attacker can intercept Bluetooth audio, and impersonate the device

## Wi-Fi Credientials

The SSID and passwords of Wi-Fi stations are stored in NVS memory.

These can be set by the web browser front-end, and when doing so, it is transported while encrypted.

Wi-Fi SSIDs are transmitted from internal server to browser but not the passwords. SSID passwords are considered write only and never read out by the front-end.

If Stolen: attacker can connect to the Wi-Fi network that has been compromised

## Session Authentication and Encryption

 * session challenge: 32 bytes, random, not secret
 * session response from client: 32 bytes, not secret
 * session response from server: 32 bytes, not secret
 * session salt: 32 bytes, not secret
 * session key: 32 bytes, secret, never transmitted

The session response from the client is derived from the network-key and the session challenge.

The session response from the server is derived from the assumed session response from the client, it is precomputed. The client uses this to verify the server is trustworthy.

The session salt is 32 bytes, with 16 bytes provided by the server and 16 bytes provided by the client.

The session key is derived from the network-key and the session salt. This is used to encrypt all payloads once available. The encryption shall use a nonce.

AES-GCM will be used, so authentication is included in the encryption.

The nonce used in AES-GCM will be 12 bytes, 10 bytes will be random, the last 2 bytes is a uint16_t counter. The server will encrypt with odd counter numbers, the client will encrypt with even counter numbers. The numbers must always go up. If the nonce does not go up as expected, the session is immediately invalidated (this means there's a message limit when the counter overflows).

The session challenge is also used as a session identifier.

If Stolen: leaking the session key can leak Wi-Fi passwords; the network-key and the master-key-pair are not practically compromised

## File Encryption

Encrypted `*.rec` recordings have fixed size packets. Each packet is encrypted entirely, using the filecrypt-key. Security-level-0 recordings are plain stereo PCM `.wav` files and do not use this packet format.

The packet header has some static features like a magic and source identifier, some parts that are dynamic like a timestamp and sequence number, plus 64 bits of random nonce.

## Authentication Over HTTP/HTTPS Request Headers

For all requests between the ESP32 and the cloud servers, the current time (UTC) is to be included, and a hash of that time (with the network-key) is to be included. The cloud server can verify that the time is in-sync within +/- 2 minutes, and infer that the network-key is correct.

For all requests from a web browser to the ESP32's web server, the current time (UTC) is to be included in plain-text, the payload is encrypted with the transaction-key and the payload includes the same current time. The ESP32's web server can verify that the transaction-key is correct based on the payload decrypting correctly.

The server does not actually care about the time reported being in-sync or not, as setting the correct time (or time server) is a part of possible administrative tasks.

## Front-End Authentication

The user will type in their password, this gets hashed into the network-key, but neither of these are transmitted.

The displayed page will be void of any data at this point. The fields on the page are all populated via Ajax after authentication completes.

The ESP32 backend will issue a session-challenge, the front-end uses the network-key to generate a session-response.

The front-end uses the session-response to issue a Ajax request to fetch the data required to populate the front-end page. This is the only time the session-response is used, as it can be easily captured. The new data will contain a session-salt.

If either is stolen: the keys are still safe; attacker can view file lists and SSID names and BT host information.

## Tamper Evidence Code

The following items:

 * filecrypt-key
 * network-key
 * network configuration binary

are hashed together. The user is shown a part of that hash on the LCD screen. If the code is not what the user remembers, the user can suspect the device has been tampered with.

Note: Bluetooth data is excluded because it is expected to change very frequently, but this does mean the device can be paired to malicious Bluetooth devices, which, at most, can listen in to the microphone.

The primary fear is that there is a new malicious cloud upload destination configured.

It is advised that this code is not shown to an attacker. A pin entry screen can prevent this.

If Stolen: attacker can make another counterfeit device that shows the same tamper-evidence-code, the user will trust this device, old encrypted files are not compromised because the filecrypt-key is not compromised, but the user can start recording audio into a counterfeit device, which can do anything it wants with it

## Touch Screen Pin Login

Under consideration

If implemented...

A 6 digit pin input is requested on boot

3 quick attempts allowed, after that it will have a cooldown to prevent brute forcing. The default Wi-Fi soft-AP can be activated so that the user either review the pin or reset the whole device.

The pin is derived from the network-key, it can be shown on the web administrative page, it cannot be changed. To change it, the network-key must be changed, and thus, forcing the refresh of all other parts of the security chain

If Stolen: network-key is not compromised (nor the filecrypt-key); attacker can connect to any bonded BT host; attacker can connect to Wi-Fi stations in memory; no keys are exposed directly; Wi-Fi passwords can be compromised by analyzing the Wi-Fi connections

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
