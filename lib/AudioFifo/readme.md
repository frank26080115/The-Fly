Implements an audio FIFO buffer suitable for this project

The internal storage of the buffer is by default mono 16 bit signed PCM assumed to be 16 kHz sample rate

There are wrapper functions that will automatically do down-mixing or resampling or etc

The FIFO can be choked or muted and the user will not need to care about the FIFO's internal state in these cases. 

Muted means any data enqueued become zeros, and dequeueing results in zeros. Choked means enqueue calls do nothing.

There is a watermark feature, if the watermark is not met, the FIFO pretends to be empty. There's an internal state machine tracking this behaviour.

This FIFO is thread safe
