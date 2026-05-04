Handles bluetooth connections, including pairing

Namespace called `BtManager`

State machine:

- Idle
- Connecting
- Connected
- Reconnecting
- Pairing
- Waiting for Incoming Connection

the bluetooth profile being implemented is only HFP

The audio callback functions are implemented in another module, but this module will be provided them as function pointers

Also include a callable function to issue a “pick up phone” command 

Public functions:

- Connect to MAC
- Start pairing
- Start waiting for incoming connection
- Disconnect (also ends pairing or any connection attempts)
- Get State

upon successful pairing, the caller is notified of the MAC and name of the paired device. This module does not handle any files that store such data.

When finished pairing, immediately disconnect.

If attempting to connect to a device that we are not bonded with, return an error code
