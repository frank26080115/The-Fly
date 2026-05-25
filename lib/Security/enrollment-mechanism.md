# Enrollment

Enrollment is how the device is registered with the cloud server. It is how the cloud server knows what the filecrypt-key is.

## Goals and Justification

For security, the policy is to distrust the human. Phishing attacks, including ones that involve fake hardware, must not compromise the filecrypt-key. This means the filecrypt-key is completely randomly generated instead of depending on a password.

The password, if compromised, can leak the network-key. This network-key is only able to allow the attacker to see some Wi-Fi credentials and Bluetooth identities. The network-key also allows the attacker to upload files to cloud servers, but the files cannot be decrypted because the attacker does not have the filecrypt-key to do the encryption.

This implementation means the filecrypt-key cannot be phished

## Workflow

The ESP32 sends an enrollment request to the cloud server.

Included data:

 * filecrypt-key
 * network-key
 * date and time
 * device MAC

This gets saved to a database table row.

The enrollment confirmation code is derived from the combination of filecrypt-key, network-key, and device MAC. This is done both on the ESP32 and on the cloud server.

The enrollment confirmation code is shown to the user on the LCD screen.

On the cloud server, the user uses their password to derive the network-key, and enters the enrollment confirmation code as shown on the screen. The server software can confirm that it found the right enrollment table row in the database, and mark it as approved.

Now files can be decrypted with that filecrypt-key. As the files are timestamped and the enrollment has a time associated, different filecrypt-keys can be used to decrypt different files.

## Security Measures

This can only occur over HTTPS. Interception of the data, or the impersonation of a server, are both considered a threat outside the scope of our security efforts.

URL of the cloud server is shown prominently to the user, as to prevent somebody from configuring a different server and having the user send the sensitive data to the wrong server. The configuration of the device is protected, but just in case.

The filecrypt-key is regenerated upon any cloud server configuration change. This prevents old keys from being sent to newly configured malicious servers.

The LCD's GUI will make it really obvious that the configuration has changed. A hash code that encompasses all configuration data is shown to the user. The user is responsible for monitoring this hash code, tamper-evidence-code.

Additional protections such as a pin code login can prevent this tamper-evidence-code from being seen by a thief. This is under consideration.
