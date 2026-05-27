Things that the user needs to be able to do:

 * pair with Bluetooth host device
 * unpair Bluetooth
 * initiate Wi-Fi connection to access point
 * initiate Wi-Fi access point and wait for incoming connection
 * change volume during actual calls
 * mute or unmute mic

The top of the screen always shows the data and time, and the battery level. If the disk space is low, there will be a warning on the top bar.

The home screen shall have buttons for the actions the user might use the most often.

The screen while a recording is happening shall use a thick bright red border as a talley light.

The user can instantly start a memo recording, while the recording is happening, the user can switch the context between several memo-type, such recording as a reminder vs recording an idea.

```mermaid
flowchart TD
    Boot --> Splash
    Splash --> Main
    Splash -->|incoming connection| Recording
    Splash -->|error| ErrorDialog

    Main --> Recording
    Main --> Bluetooth
    Main --> WiFi
    Main --> InfoScreen
    InfoScreen --> Main

    Bluetooth --> BTConnect
    Bluetooth --> BTPair
    Bluetooth --> BTInfo
    Bluetooth --> BTUnpairConfirm

    BTConnect -->|established| Recording
    BTConnect -->|cancel| Main

    BTPair -->|pair complete| BTPaired
    BTPair -->|cancel| Main
    BTPair -->|error| BTErrorDialog
    BTPaired --> Bluetooth

    BTInfo --> Bluetooth
    BTUnpairConfirm --> Bluetooth

    BTErrorDialog --> Main

    WiFi --> WiFiConnect
    WiFi --> WiFiAP
    WiFi --> WiFiInfo

    WiFiConnect -->|established| Cloud
    WiFiConnect -->|error| WifiErrorDialog

    WifiErrorDialog --> WiFi

    WiFiAP -->|reset| Boot
    WiFiAP -->|password reset| PasswordReset
    WiFiAP -->|memory reset| MemoryReset
    PasswordReset -->|reset| Boot
    MemoryReset -->|reset| Boot
    WiFiInfo --> WiFi

    Cloud --> CloudUpload
    Cloud --> CloudEnroll

    CloudUpload -->|cancel| Cloud
    CloudUpload -->|done| CloudUploadConfirmation
    CloudUpload -->|error| CloudErrorDialog
    CloudUploadConfirmation -->|reset| Boot

    CloudEnroll --> CloudEnrollConfirmation
    CloudEnroll -->|error| CloudErrorDialog
    CloudEnrollConfirmation --> Cloud

    Recording -->|stop| FinishedConfirmation
    Recording -->|error| ErrorDialog
    FinishedConfirmation --> Main

    CloudErrorDialog --> Cloud

    ErrorDialog -->|not fatal| Main
    ErrorDialog -->|fatal| Boot

    note1["Global event policy:
    incoming Bluetooth connection → Recording
    applies from any screen
    except when Wi-Fi radio owns the radio"]

```