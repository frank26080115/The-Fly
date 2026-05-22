The device has an internally known list of Bluetooth hosts, along with their friendly names.

The GUI is populated with this list, the user can choose one of these hosts to connect to, or enable pairing mode.

During normal Bluetooth operation, the device is connectable but not discoverable. A previously paired phone can manually connect to it from its saved Bluetooth device list, but new phones should not find it in a discovery scan.

Pairing mode temporarily makes the device discoverable as well as connectable. After pairing succeeds, the device returns to connectable/non-discoverable operation.

If a connection is established, then proceed.

If pairing is established, the internal memory is updated. If the device has no known name internally and the name can be retrieved, then the name is saved. If the name has been edited by the user already, the name is not overwritten by pairing.

After connecting, a new audio file recording is started. The file name follows our naming convention.

There are two FIFOs heading into the file. The thread will drain both of them and stream them into the file interleaved (if possible). Every chunk being streamed has a header to indicate the source of the chunk.

There is a stop button, it will immediately stop the recording, and also disconnect from Bluetooth. The file is truncated to the correct size and then closed.

During recording, the GUI is showing

 * the tally light (the red border around the whole screen)
 * current date and time
 * battery status
 * current recording length
 * context information (maybe it's the BT name, the caller name if available, etc)
 * mute/unmute status
 * output volume
 * mic level peaking meter
