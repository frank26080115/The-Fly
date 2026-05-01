There is an upstream and downstream sound source. It is possible that the two streams don't have matching sample rates or bit depths. It is also very desirable for the two streams to be recorded separately, but into one file.

The file will be a sequence of packets, and each packets has a header and a payload. The header describes the payload, and the payload is raw.

Proposed header
```
typedef struct PACKED {
    uint32_t magic;        // helps resync
    uint32_t timestamp;    // millis()
    uint8_t  counter;      // or sample counter
    uint8_t  code:4;       // 0xF for special marker/event, other codes used to describe direction and sample rate
    uint8_t  flags:4;      // codec, underrun, etc.
    uint32_t payload_len;  // bytes following header
}
audio_pkt_header_t;
```