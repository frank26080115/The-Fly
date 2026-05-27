There is an upstream and downstream sound source. It is possible that the two streams don't have matching sample rates or bit depths. It is also very desirable for the two streams to be recorded separately, but into one file.

The file will be a sequence of packets, and each packets has a header and a payload. The header describes the payload, and the payload is raw.

Proposed header is defined in `inc\defs.h` as `file_packet_t`

When encrypted, each packet is individually encrypted, not the file as a whole
