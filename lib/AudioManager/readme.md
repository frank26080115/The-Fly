Handles pumping of audio data to/from I2S, volume control, push-to-talk I2S reconfiguration, etc

Only one instance needed so implement with a namespace instead of a OOP class

This module owns all 4 FIFOs:

 * Bluetooth to Speaker
 * Bluetooth to File
 * Mic to Bluetooth
 * Mic to File

It only needs to pump the `Bluetooth to Speaker` and `Mic to Bluetooth` FIFOs. Make these two different functions.

Pumping `Bluetooth to Speaker` (`pump_bt2s`) means dequeuing and then putting into I2S DMA. Make sure both the FIFO has enough data and the I2S can accept the data before actually performing the dequeue and the write.

Pumping `Mic to Bluetooth` (`pump_mic2bt`) means checking if the I2S has data, if so, enqueue it. The `hfp_outgoing_audio` callback will handle the dequeuing.

The implementation of `void hfp_incoming_audio(const uint8_t* buf, uint32_t len)` and `uint32_t hfp_outgoing_audio(uint8_t* buf, uint32_t len)` will live here. But they will be registered as callbacks from another place in the project.

`hfp_incoming_audio` will enqueue data, and then immediately call the `Bluetooth to Speaker` pump function. This makes sense because the FIFO is actually implemented with a minimum watermark feature.

`hfp_outgoing_audio` will dequeue from the mic FIFO and also call the corresponding pump function.

Also, when I2S is initialized, use `i2s_channel_register_event_callback` to register a callback that calls both pumping functions.

The two pumping functions are also going to be called in a task thread.

Just a reminder, all the audio is 16 KHz sample rate, 16 bit PCM. The AudioFifo class handles upsampling and channel duplication.

There are two hardware implementation cases:

 1. M5Stack Core 2 internal speaker and internal mic, so we need push-to-talk
 2. external WM8960

This module does not need to check the buttons for anything. It needs to provide the functions that the GUI calls.

Volume control is also called in from the GUI, have functions for volume up, volume down, volume set, and volume get. The volume is treated like a 0 to 10 number.
