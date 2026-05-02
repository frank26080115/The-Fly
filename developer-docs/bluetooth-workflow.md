There is a JSON file called `bluetooth.json` on the microSD card. This file contains multiple hosts. Each host is described by a friendly name and a MAC address.

The GUI is populated with this list, the user can choose one of these hosts to connect to, or to enable pairing (advertising) mode.

If a connection is established, then proceed.

If pairing is established, this JSON file is updated with the new entry. If the host's name is not available, the user can always edit the JSON file later. The name in the JSON file takes priority.

After connecting, a new audio file recording is started. The file name follows our naming convention. The file is first "grown" to a large size, then the write seek pointer is reset to 0, then recording can actually begin.

There are two FIFOs heading into the file. The thread will drain both of them and stream them into the file interleaved. Every chunk being streamed has a header to indicate the source of the chunk.

There is a stop button, it will immediately stop the recording, and also disconnect from Bluetooth. The file is truncated to the correct size and then closed.

During recording, the GUI is showing

 * the tally light (the red border around the whole screen)
 * current date and time
 * battery status
 * current recording length
 * connected bluetooth device name
 * mute/unmute status
 * output volume
 * mic level peaking meter
 * hints to what each button does