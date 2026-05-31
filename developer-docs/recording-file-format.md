For unencrypted audio recording files, the `.wav` file format is used directly. The file is 16 kHz, stereo, 16-bit little-endian signed PCM. The left channel is microphone audio and the right channel is Bluetooth audio; unavailable channels are filled with silence. Only one audio "chunk" is used.

For encrypted audio recording files. The structure is similar but data is encrypted in chunks.

The RIFF header is the first chunk to be encrypted. The RIFF header data will reflect the same audio format, but with an unknown length.

16 bit signed PCM stereo data is 