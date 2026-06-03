Handles bluetooth connections, including pairing

Namespace called `BtManager`

State machine:

- Idle
- Connecting
- Connected
- Audio Available
- Reconnecting
- Pairing

the bluetooth profile being implemented is only HFP

The audio callback functions are implemented in another module, but this module will be provided them as function pointers

Also include a callable function to issue a “pick up phone” command 

Public functions:

- Connect to MAC
- Start pairing
- Disconnect (also ends pairing or any connection attempts)
- Shutdown Bluetooth for Wi-Fi mode
- Get State

upon successful pairing, the caller is notified of the MAC and name of the paired device. This module does not handle any files that store such data.

The device normally stays connectable but not discoverable. Pairing mode temporarily makes it discoverable, then returns it to connectable/non-discoverable mode.

If attempting to connect to a device that we are not bonded with, return an error code

### State Diagram

Diagram generated June 2 2026
```mermaid
stateDiagram-v2
    state "Audio Available" as AudioAvailable

    [*] --> Idle: init()

    Idle --> Connecting: connectToMac(bonded MAC)
    Idle --> Pairing: startPairing()
    Idle --> Idle: connectToMac precheck fails
    Idle --> Idle: shutdown()

    Pairing --> Idle: paired callback handled
    Pairing --> Connecting: host opens HFP during pairing
    Pairing --> Pairing: auth failed
    Pairing --> Idle: disconnect()

    Connecting --> Connecting: RFCOMM/control connected
    Connecting --> Connected: service level connection
    Connecting --> Idle: connect failed or disconnect()

    Connected --> AudioAvailable: SCO audio connected
    AudioAvailable --> Connected: SCO audio disconnected

    Connected --> Reconnecting: unexpected HFP disconnect
    AudioAvailable --> Reconnecting: unexpected HFP disconnect
    Reconnecting --> Connecting: poll() reconnect attempt
    Reconnecting --> Reconnecting: reconnect attempt fails
    Reconnecting --> Idle: disconnect()

    Connected --> Idle: disconnect() or shutdown()
    AudioAvailable --> Idle: disconnect() or shutdown()

    note right of Idle
        Normal radio mode is connectable
        and non-discoverable.
    end note

    note right of Pairing
        Pairing temporarily switches to
        connectable and discoverable.
    end note

    note right of AudioAvailable
        HFP control is connected and
        SCO audio callbacks can run.
    end note
```
