# Level -1, Developer Mode

NVS can be read out by debug code if needed

Authentication functionality may be bypassed

Secrets might be stored in plain-text, may be hard-coded

For testing only. The existence of this level number is just so the GUI can show it to the user.

# Level 0

Equivalent to a plain consumer voice recorder.

Files are not encrypted.

Wi-Fi credentials are write-only without authentication. Encryption depends on being on a Wi-Fi soft AP that uses WPA3, only allowing one client. The AP's password can be fixed.

# Level 1

Files are encrypted.

Decryption depends on enrollment to cloud server.

Wi-Fi credentials are write-only, with authentication. Password for administration is tied to both filecrypt-key and network-key.

Changing password requires re-enrollment, this seems reasonable and easy to understand.

# Level 2

Includes level-1 specifications

Filecrypt-key is randomly regenerated upon any network configuration change, and requires re-enrollment.

Tamper evidence code is shown when requested

# Level 3

Includes level-2 specifications

Pin code login required
