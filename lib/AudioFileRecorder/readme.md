this module is responsible for writing audio data into a file

this doesn't need to be a OOP class as there is only one instance.

when initialized, two instances of AudioFifo is attached by reference, and remembered. One is for Bluetooth-to-file, one is for mic-to-file. The microSD card and its file system is readied as well.

there will be a function to start recording a new file. If a previous file is already opened, close it first. The new file will have a name following the naming convention designed, which will need the RTC to get the time. The type of recording is also specified in this function call.

When the file is created, use the currently existing `bool grow_file(File &file, uint64_t size)`, move that function into here

There is a pump function that is called as often as possible. It pumps audio data from the two FIFOs into the file, using `file_packet_t`. Interleave the two FIFOs. In Bluetooth recordings, both FIFOs are used, interleave their packets. In memo (or similar) modes, only the mic FIFO will have data. It's fine to not care about this as the dequeue function of the FIFO will simply return nothing.

The policy for pumping is that it should drain the FIFOs at least below 10% on each call. But it must also track the time passing and never take more than 20ms per call. (if 20ms per call isn't enough then the FIFO will eventually overflow and an error flag will be set)

There is a stopping function, which marks the FIFOs as "do not queue", then drains the FIFOs into the file, then truncates the file to the correct size and closes the file (or which ever order these actions needs to take). The file starting function should just use this for convenience.

The pump function might be called even though no file is open, it should immediate return if this is the case. The caller of the pump function does not know the state of the device.
