# WAV File and Encrypted WAV File

For unencrypted audio recording files, the `.wav` file format can be used directly. The file is 16 kHz, stereo, 16-bit little-endian signed PCM. The left channel is microphone audio and the right channel is Bluetooth audio; unavailable channels are filled with silence. Only one audio "chunk" is used.

For encrypted audio recording files. The structure is similar but data is encrypted in chunks.

The RIFF header is the first chunk to be encrypted. The RIFF header data will reflect the same audio format, but the size of the file as indicated by the RIFF header shall be the maximum allowed value. If decrypted completely, software can put a correct value back in later.

Then, the raw audio data is encrypted as a packet. The size is configurable through `inc\conf.h` and `inc\defs.h`

# MP3 Files

There is a firmware build option to use MP3 files instead.

The file should use 64 kbps constant bit-rate mode (CBR). The audio represented is 16 kHz sample rate, 16 bit stereo.

When encrypted, several MP3 frames are packed together into one encrypted packet. See `inc\conf.h` and `inc\defs.h` for details
