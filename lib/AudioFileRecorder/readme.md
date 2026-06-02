this module is responsible for writing audio data into a file

it supports MP3 compression and encryption

the files are named with the context and the current time

there are two FIFOs going from the audio manager into the file, this data is mixed together into a stereo stream as appropriate

at the end of file writing, it will drain the FIFOs if the stop is not emergency, and then also change the file name if the context of the recording has been changed mid-recording
