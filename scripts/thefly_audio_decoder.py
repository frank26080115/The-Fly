#!/usr/bin/env python3
from __future__ import annotations

"""
Decode The Fly `.rec` audio recording format into a 16 kHz, 16-bit, stereo WAV.

The input format is `file_packet_t` from `inc/defs.h`. Packets are fixed-size,
little-endian records with permanent sequence and FIFO count fields.
"""

"""
This tool decodes the custom format that The Fly records in and outputs a practical WAV file.

Use argparse, allow the user to specify an input file (ideally it should be `.rec` extension, warn if not). The output file should be the input file path but with the file extension swapped for `.wav`

The output wav file is 16 KHz sample rate, 16 bit PCM, stereo. The input format is determined by the headers detected in the input file.

The input format is described by `file_packet_t` in `inc\\defs.h`. The way it is recorded is shown in `lib\\AudioFileRecorder\\AudioFileRecorder.cpp` function `AudioFileRecorder::write_packet`.

The WAV file is stereo, and `file_packet_t` has a field `src`. If `src` is an odd number, the data goes into the left channel, if `src` is an even number, the data goes into the right channel. If `src` is 0 or 1, the format is 16 kHz 16 bit PCM mono...

 * 2 or 3: 32 kHz 16 bit PCM mono
 * 4 or 5: 32 kHz 16 bit PCM stereo
 * 6 or 7: 48 kHz 16 bit PCM mono
 * 8 or 9: 48 kHz 16 bit PCM stereo
(these are for future use, current code only writes 0 or 1)

If a stereo stream is the input, then determine if both channels are used (both are not silent), if both are used then down-mix it into mono for the WAV file. If only one side is used, then for that chunk, use only that side.

As both channels are decoded, there is a millisecond timestamp available from the packet headers. Ideally both channels advance in time at the same rate but we do need to handle lost packets by stuffing in silence.

There is both a timestamp and a shared packet counter available in the packet header to help determine if one channel is lagging significantly, the counter should increment by 2 for each channel if both are synchronized and steady. If one channel is getting too many consecutive increments of 1 in its counter, it means the other channel is missing data.

(the timestamp is not that reliable as the thread that writes to the card may face extremely long blocking card flushes)

The silent insertion should ideally be at the point when the channel picks back up after a gap. The amount is such that it catches up to the other channel exactly by sample count. If only one channel is active, then there's no way to catch up by sample count, then just use the millisecond timestamp to know how much to pad.

Only do the stuffing when the current packet channel detects a gap. Check for gaps only, not consecutiveness. If you also need a time specification, say 200 milliseconds is the threshold (microSD card test shows under 60 millisecond write block times).

Print a warning in the console when this occurs, indicating the time in the final file where it happened and the length in milliseconds that was filled in.

If the `magic` header is missing, print a warning (indicate the byte index in the file where this occured) and scan forward in the file until the magic header is found again (indicate the byte index in the file where it is found again).

Track spikes in `fifo_cnt`, timestamps and amount. Spikes meaning every peak of "was going up, now going down". I'm expecting the level to be 0 always.

Use an intermediate file with the `.pcm` extension for the first decode pass. Then add a file header for the actual `.wav` file.

"""

import argparse
import struct
import sys
import tempfile
import wave
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO, Dict, Iterable, List, Optional, Sequence, Tuple

import sectools


FILE_PACKET_HEADER_MAGIC = 0xDEADBEEF
FILE_PACKET_PAYLOAD_MAX = 256
AUDSRC_META_TEXT = 0xAA
OUTPUT_SAMPLE_RATE_HZ = 16000
OUTPUT_CHANNELS = 2
SAMPLE_WIDTH_BYTES = 2
FIFO_BACKLOG_SPIKE_WARNING_THRESHOLD_SAMPLES = 3072
SEQUENCE_GAP_WITHOUT_PADDING_WARNING_DELTA = 16

PACKET_FLAG_FIFO_OVERFLOW = 1 << 0
PACKET_FLAG_FIFO_UNDERFLOW = 1 << 1

MAGIC_BYTES = struct.pack("<I", FILE_PACKET_HEADER_MAGIC)
HEADER_STRUCT = struct.Struct("<IBBIIIH")
HEADER_SIZE = HEADER_STRUCT.size
PAYLOAD_BYTES = FILE_PACKET_PAYLOAD_MAX * SAMPLE_WIDTH_BYTES
PACKET_SIZE = HEADER_SIZE + PAYLOAD_BYTES

ZERO_SAMPLE_BLOCK = b"\x00\x00" * 4096


@dataclass
class Packet:
    offset: int
    src: int
    flags: int
    ms_timestamp: int
    sequence_num: int
    fifo_cnt: int
    payload_length: int
    payload: bytes


@dataclass
class SourceFormat:
    sample_rate_hz: int
    stereo: bool


@dataclass
class ChannelState:
    name: str
    file: BinaryIO
    cursor_samples: int = 0
    packet_count: int = 0
    last_sequence_num: Optional[int] = None
    last_timestamp_ms: Optional[int] = None
    last_output_samples: int = 0
    peak_abs: int = 0
    peak_value: int = 0
    peak_sample: Optional[int] = None


@dataclass
class FifoPoint:
    src: int
    sequence_num: int
    timestamp_ms: int
    file_offset: int
    fifo_cnt: int
    payload_length: int
    backlog_samples: int


class FifoSpikeTracker:
    def __init__(self, src: int) -> None:
        self.src = src
        self._older: Optional[FifoPoint] = None
        self._previous: Optional[FifoPoint] = None
        self.spike_count = 0

    def add(self, point: FifoPoint) -> None:
        if (
            self._older is not None
            and self._previous is not None
            and self._previous.backlog_samples > self._older.backlog_samples
            and self._previous.backlog_samples > point.backlog_samples
        ):
            self.spike_count += 1
            if self._previous.backlog_samples > FIFO_BACKLOG_SPIKE_WARNING_THRESHOLD_SAMPLES:
                warn(
                    "fifo backlog spike: "
                    f"src={self.src} seq={self._previous.sequence_num} "
                    f"timestamp={self._previous.timestamp_ms}ms "
                    f"offset={self._previous.file_offset} "
                    f"backlog={self._previous.backlog_samples} samples "
                    f"fifo_cnt={self._previous.fifo_cnt} payload={self._previous.payload_length}"
                )

        self._older = self._previous
        self._previous = point


def warn(message: str) -> None:
    print(f"warning: {message}", file=sys.stderr)


def unsigned_delta_u32(newer: int, older: int) -> int:
    return (newer - older) & 0xFFFFFFFF


def clip_int16(value: int) -> int:
    if value < -32768:
        return -32768
    if value > 32767:
        return 32767
    return value


def source_format(src: int) -> Optional[SourceFormat]:
    if src in (0, 1):
        return SourceFormat(16000, False)
    if src in (2, 3):
        return SourceFormat(32000, False)
    if src in (4, 5):
        return SourceFormat(32000, True)
    if src in (6, 7):
        return SourceFormat(48000, False)
    if src in (8, 9):
        return SourceFormat(48000, True)
    return None


def output_channel_index(src: int) -> int:
    # Spec: odd sources go left, even sources go right.
    return 0 if src % 2 else 1


def parse_header(data: bytes) -> Tuple[int, int, int, int, int, int, int]:
    return HEADER_STRUCT.unpack_from(data)


def plausible_header(data: bytes) -> bool:
    if len(data) < HEADER_SIZE:
        return False
    magic, src, _flags, _timestamp, _sequence, _fifo_cnt, payload_length = parse_header(data)
    if magic != FILE_PACKET_HEADER_MAGIC:
        return False
    if src == AUDSRC_META_TEXT:
        return payload_length <= PAYLOAD_BYTES
    return src <= 9 and payload_length <= FILE_PACKET_PAYLOAD_MAX


def find_next_packet(stream: BinaryIO, start_offset: int) -> Optional[int]:
    stream.seek(start_offset)
    chunk_start = start_offset
    overlap = b""

    while True:
        chunk = stream.read(64 * 1024)
        if not chunk:
            return None

        data = overlap + chunk
        data_start = chunk_start - len(overlap)
        search_at = 0

        while True:
            index = data.find(MAGIC_BYTES, search_at)
            if index < 0:
                break

            candidate_offset = data_start + index
            saved_position = stream.tell()
            stream.seek(candidate_offset)
            header = stream.read(HEADER_SIZE)
            stream.seek(saved_position)

            if plausible_header(header):
                return candidate_offset

            search_at = index + 1

        chunk_start += len(chunk)
        overlap = data[-(len(MAGIC_BYTES) - 1) :]


def iter_packets(input_path: Path) -> Iterable[Packet]:
    with input_path.open("rb") as stream:
        while True:
            offset = stream.tell()
            data = stream.read(PACKET_SIZE)
            if not data:
                return

            if len(data) < HEADER_SIZE:
                warn(f"trailing {len(data)} bytes at byte {offset}; ignoring")
                return

            if not plausible_header(data):
                warn(f"packet magic/header missing at byte {offset}")
                next_offset = find_next_packet(stream, offset + 1)
                if next_offset is None:
                    warn("no later packet header found; stopping decode")
                    return
                warn(f"resynchronized at byte {next_offset}")
                stream.seek(next_offset)
                continue

            if len(data) < PACKET_SIZE:
                warn(f"truncated packet at byte {offset}; expected {PACKET_SIZE} bytes, got {len(data)}")
                return

            magic, src, flags, timestamp, sequence, fifo_cnt, payload_length = parse_header(data)
            if magic != FILE_PACKET_HEADER_MAGIC:
                raise AssertionError("plausible_header accepted a packet with bad magic")

            yield Packet(
                offset=offset,
                src=src,
                flags=flags,
                ms_timestamp=timestamp,
                sequence_num=sequence,
                fifo_cnt=fifo_cnt,
                payload_length=payload_length,
                payload=data[HEADER_SIZE:PACKET_SIZE],
            )


def unpack_payload(packet: Packet) -> List[int]:
    sample_count = min(packet.payload_length, FILE_PACKET_PAYLOAD_MAX)
    if packet.payload_length > FILE_PACKET_PAYLOAD_MAX:
        warn(f"payload length too large at byte {packet.offset}: {packet.payload_length}; clamped")
    if sample_count == 0:
        return []
    sample_bytes = packet.payload[: sample_count * SAMPLE_WIDTH_BYTES]
    return list(struct.unpack(f"<{sample_count}h", sample_bytes))


def stereo_to_mono(samples: Sequence[int], packet: Packet) -> List[int]:
    if len(samples) < 2:
        return []
    if len(samples) % 2:
        warn(f"odd stereo payload sample count at byte {packet.offset}; dropping final sample")

    frame_count = len(samples) // 2
    left_used = any(samples[i * 2] != 0 for i in range(frame_count))
    right_used = any(samples[i * 2 + 1] != 0 for i in range(frame_count))

    mono: List[int] = []
    for index in range(frame_count):
        left = samples[index * 2]
        right = samples[index * 2 + 1]
        if left_used and right_used:
            mono.append(clip_int16((left + right) // 2))
        elif left_used:
            mono.append(left)
        elif right_used:
            mono.append(right)
        else:
            mono.append(0)
    return mono


def resample_to_output_rate(samples: Sequence[int], sample_rate_hz: int, carry: List[int]) -> List[int]:
    if sample_rate_hz == OUTPUT_SAMPLE_RATE_HZ:
        return list(samples)

    if sample_rate_hz % OUTPUT_SAMPLE_RATE_HZ != 0:
        raise ValueError(f"unsupported sample rate {sample_rate_hz}; expected an integer multiple of {OUTPUT_SAMPLE_RATE_HZ}")

    factor = sample_rate_hz // OUTPUT_SAMPLE_RATE_HZ
    pending = carry + list(samples)
    usable_count = (len(pending) // factor) * factor
    output: List[int] = []

    for index in range(0, usable_count, factor):
        output.append(clip_int16(round(sum(pending[index : index + factor]) / factor)))

    carry[:] = pending[usable_count:]
    return output


def decode_packet_samples(packet: Packet, resample_carry_by_src: Dict[int, List[int]]) -> List[int]:
    fmt = source_format(packet.src)
    if fmt is None:
        warn(f"unsupported source {packet.src} at byte {packet.offset}; skipping packet")
        return []

    samples = unpack_payload(packet)
    if fmt.stereo:
        samples = stereo_to_mono(samples, packet)

    carry = resample_carry_by_src.setdefault(packet.src, [])
    return resample_to_output_rate(samples, fmt.sample_rate_hz, carry)


def decode_meta_text(packet: Packet) -> str:
    byte_count = min(packet.payload_length, PAYLOAD_BYTES)
    if packet.payload_length > PAYLOAD_BYTES:
        warn(f"metadata text too large at byte {packet.offset}: {packet.payload_length}; clamped")
    text_bytes = packet.payload[:byte_count].rstrip(b"\x00")
    return text_bytes.decode("utf-8", errors="replace")


def samples_to_bytes(samples: Sequence[int]) -> bytes:
    if not samples:
        return b""
    return struct.pack(f"<{len(samples)}h", *samples)


def write_silence(stream: BinaryIO, sample_count: int) -> None:
    remaining = sample_count
    while remaining > 0:
        samples = min(remaining, len(ZERO_SAMPLE_BLOCK) // SAMPLE_WIDTH_BYTES)
        stream.write(ZERO_SAMPLE_BLOCK[: samples * SAMPLE_WIDTH_BYTES])
        remaining -= samples


def insert_silence(state: ChannelState, sample_count: int, reason: str, packet: Packet) -> None:
    if sample_count <= 0:
        return
    start_seconds = state.cursor_samples / OUTPUT_SAMPLE_RATE_HZ
    duration_ms = sample_count * 1000.0 / OUTPUT_SAMPLE_RATE_HZ
    warn(
        f"inserted {duration_ms:.1f} ms silence in {state.name} "
        f"at {start_seconds:.3f} s before src={packet.src} seq={packet.sequence_num} ({reason})"
    )
    write_silence(state.file, sample_count)
    state.cursor_samples += sample_count


def maybe_insert_gap(
    packet: Packet,
    state: ChannelState,
    other_state: ChannelState,
    base_timestamp_ms: int,
    gap_threshold_ms: float,
) -> None:
    threshold_samples = int(round(gap_threshold_ms * OUTPUT_SAMPLE_RATE_HZ / 1000.0))

    if state.last_sequence_num is None:
        start_delta_ms = unsigned_delta_u32(packet.ms_timestamp, base_timestamp_ms)
        if start_delta_ms > gap_threshold_ms:
            pad_samples = int(round(start_delta_ms * OUTPUT_SAMPLE_RATE_HZ / 1000.0))
            insert_silence(state, pad_samples, "initial timestamp gap", packet)
        return

    sequence_delta = unsigned_delta_u32(packet.sequence_num, state.last_sequence_num)
    previous_timestamp_ms = state.last_timestamp_ms if state.last_timestamp_ms is not None else packet.ms_timestamp
    timestamp_delta_ms = unsigned_delta_u32(packet.ms_timestamp, previous_timestamp_ms)
    expected_timestamp_delta_ms = state.last_output_samples * 1000.0 / OUTPUT_SAMPLE_RATE_HZ
    timestamp_gap_ms = timestamp_delta_ms - expected_timestamp_delta_ms

    sequence_gap = sequence_delta > 2
    timestamp_gap = timestamp_gap_ms > gap_threshold_ms
    if not sequence_gap and not timestamp_gap:
        return

    pad_samples = 0
    reasons: List[str] = []

    if sequence_gap:
        reasons.append(f"sequence delta {sequence_delta}")
    if timestamp_gap:
        reasons.append(f"timestamp gap {timestamp_gap_ms:.1f} ms")

    if other_state.cursor_samples > state.cursor_samples:
        pad_samples = other_state.cursor_samples - state.cursor_samples
        reasons.append("catching up to other channel")
    elif timestamp_gap:
        pad_samples = int(round(timestamp_gap_ms * OUTPUT_SAMPLE_RATE_HZ / 1000.0))
        reasons.append("timestamp-derived one-channel gap")
    elif sequence_gap:
        # No other channel is ahead and the timestamp does not show a large
        # wall-clock gap. Avoid inventing timeline from sequence numbers alone.
        if sequence_delta >= SEQUENCE_GAP_WITHOUT_PADDING_WARNING_DELTA:
            warn(
                f"large sequence gap without padding: {state.name} src={packet.src} "
                f"seq={packet.sequence_num} delta={sequence_delta} at byte {packet.offset}"
            )

    if pad_samples > 0:
        if pad_samples < threshold_samples and sequence_gap:
            reasons.append("short sample-count catch-up")
        insert_silence(state, pad_samples, ", ".join(reasons), packet)


def note_packet_flags(packet: Packet) -> None:
    flag_names: List[str] = []
    if packet.flags & PACKET_FLAG_FIFO_OVERFLOW:
        flag_names.append("fifo-overflow")
    if packet.flags & PACKET_FLAG_FIFO_UNDERFLOW:
        flag_names.append("fifo-underflow")
    unknown_flags = packet.flags & ~(PACKET_FLAG_FIFO_OVERFLOW | PACKET_FLAG_FIFO_UNDERFLOW)
    if unknown_flags:
        flag_names.append(f"unknown-0x{unknown_flags:02X}")
    if flag_names:
        warn(
            f"packet flags at byte {packet.offset}: src={packet.src} seq={packet.sequence_num} "
            f"timestamp={packet.ms_timestamp}ms flags={','.join(flag_names)}"
        )


def note_fifo_point(packet: Packet, trackers: Dict[int, FifoSpikeTracker]) -> None:
    tracker = trackers.setdefault(packet.src, FifoSpikeTracker(packet.src))
    backlog_samples = max(0, packet.fifo_cnt - min(packet.payload_length, FILE_PACKET_PAYLOAD_MAX))
    tracker.add(
        FifoPoint(
            src=packet.src,
            sequence_num=packet.sequence_num,
            timestamp_ms=packet.ms_timestamp,
            file_offset=packet.offset,
            fifo_cnt=packet.fifo_cnt,
            payload_length=packet.payload_length,
            backlog_samples=backlog_samples,
        )
    )


def note_channel_peak(state: ChannelState, samples: Sequence[int]) -> None:
    for index, sample in enumerate(samples):
        sample_abs = abs(sample)
        if sample_abs > state.peak_abs:
            state.peak_abs = sample_abs
            state.peak_value = sample
            state.peak_sample = state.cursor_samples + index


def write_channel_samples(state: ChannelState, samples: Sequence[int]) -> None:
    note_channel_peak(state, samples)
    state.file.write(samples_to_bytes(samples))
    state.cursor_samples += len(samples)


def update_channel_state(state: ChannelState, packet: Packet, output_sample_count: int) -> None:
    state.packet_count += 1
    state.last_sequence_num = packet.sequence_num
    state.last_timestamp_ms = packet.ms_timestamp
    state.last_output_samples = output_sample_count


def pad_channel_to(state: ChannelState, target_samples: int) -> None:
    if state.cursor_samples >= target_samples:
        return
    pad_samples = target_samples - state.cursor_samples
    write_silence(state.file, pad_samples)
    state.cursor_samples = target_samples


def read_or_zero(stream: BinaryIO, sample_count: int) -> bytes:
    data = stream.read(sample_count * SAMPLE_WIDTH_BYTES)
    expected = sample_count * SAMPLE_WIDTH_BYTES
    if len(data) < expected:
        data += b"\x00" * (expected - len(data))
    return data


def merge_channels_to_pcm(left_path: Path, right_path: Path, pcm_path: Path, frame_count: int) -> None:
    block_frames = 4096
    with left_path.open("rb") as left, right_path.open("rb") as right, pcm_path.open("wb") as output:
        remaining = frame_count
        while remaining > 0:
            frames = min(remaining, block_frames)
            left_data = read_or_zero(left, frames)
            right_data = read_or_zero(right, frames)
            left_samples = struct.unpack(f"<{frames}h", left_data)
            right_samples = struct.unpack(f"<{frames}h", right_data)

            interleaved: List[int] = []
            for left_sample, right_sample in zip(left_samples, right_samples):
                interleaved.append(left_sample)
                interleaved.append(right_sample)
            output.write(samples_to_bytes(interleaved))
            remaining -= frames


def append_wav_info_icmt(wav_path: Path, comment: Optional[str]) -> None:
    if not comment:
        return

    comment_data = comment.encode("utf-8", errors="replace")
    if not comment_data.endswith(b"\x00"):
        comment_data += b"\x00"

    icmt_payload = comment_data
    icmt_chunk = b"ICMT" + struct.pack("<I", len(icmt_payload)) + icmt_payload
    if len(icmt_payload) % 2:
        icmt_chunk += b"\x00"

    list_payload = b"INFO" + icmt_chunk
    list_chunk = b"LIST" + struct.pack("<I", len(list_payload)) + list_payload
    if len(list_payload) % 2:
        list_chunk += b"\x00"

    with wav_path.open("r+b") as wav:
        riff_header = wav.read(12)
        if len(riff_header) != 12 or riff_header[:4] != b"RIFF" or riff_header[8:12] != b"WAVE":
            raise ValueError(f"not a RIFF/WAVE file: {wav_path}")

        wav.seek(0, 2)
        wav.write(list_chunk)
        file_size = wav.tell()
        wav.seek(4)
        wav.write(struct.pack("<I", file_size - 8))


def wrap_pcm_as_wav(pcm_path: Path, wav_path: Path, comment: Optional[str]) -> None:
    with pcm_path.open("rb") as pcm, wave.open(str(wav_path), "wb") as wav:
        wav.setnchannels(OUTPUT_CHANNELS)
        wav.setsampwidth(SAMPLE_WIDTH_BYTES)
        wav.setframerate(OUTPUT_SAMPLE_RATE_HZ)
        while True:
            data = pcm.read(64 * 1024)
            if not data:
                break
            wav.writeframes(data)
    append_wav_info_icmt(wav_path, comment)


def format_peak_report(state: ChannelState) -> str:
    if state.peak_sample is None:
        return f"{state.name} peak:     none"

    peak_seconds = state.peak_sample / OUTPUT_SAMPLE_RATE_HZ
    peak_percent = (state.peak_abs * 100.0) / 32768.0
    return (
        f"{state.name} peak:     "
        f"{state.peak_abs} ({peak_percent:.1f}% FS) "
        f"value={state.peak_value} "
        f"at {peak_seconds:.3f} s "
        f"(sample {state.peak_sample})"
    )


def decode_recording(input_path: Path, pcm_path: Path, wav_path: Path, gap_threshold_ms: float) -> None:
    resample_carry_by_src: Dict[int, List[int]] = {}
    fifo_trackers: Dict[int, FifoSpikeTracker] = {}
    base_timestamp_ms: Optional[int] = None
    info_comment: Optional[str] = None
    packet_count = 0

    left_tmp = tempfile.NamedTemporaryFile(prefix="thefly-left-", suffix=".pcm", delete=False)
    right_tmp = tempfile.NamedTemporaryFile(prefix="thefly-right-", suffix=".pcm", delete=False)
    left_path = Path(left_tmp.name)
    right_path = Path(right_tmp.name)

    states = [
        ChannelState("left", left_tmp),
        ChannelState("right", right_tmp),
    ]

    try:
        for packet in iter_packets(input_path):
            packet_count += 1
            if packet.src == AUDSRC_META_TEXT:
                info_comment = decode_meta_text(packet)
                continue

            if base_timestamp_ms is None:
                base_timestamp_ms = packet.ms_timestamp

            note_packet_flags(packet)
            note_fifo_point(packet, fifo_trackers)

            samples = decode_packet_samples(packet, resample_carry_by_src)
            if not samples:
                continue

            channel_index = output_channel_index(packet.src)
            state = states[channel_index]
            other_state = states[1 - channel_index]

            maybe_insert_gap(packet, state, other_state, base_timestamp_ms, gap_threshold_ms)
            write_channel_samples(state, samples)
            update_channel_state(state, packet, len(samples))

        for carry_src, carry in sorted(resample_carry_by_src.items()):
            if carry:
                warn(f"dropping {len(carry)} leftover resampler samples for src={carry_src}")

        frame_count = max(states[0].cursor_samples, states[1].cursor_samples)
        pad_channel_to(states[0], frame_count)
        pad_channel_to(states[1], frame_count)

        for state in states:
            state.file.flush()
            state.file.close()

        merge_channels_to_pcm(left_path, right_path, pcm_path, frame_count)
        wrap_pcm_as_wav(pcm_path, wav_path, info_comment)

        duration_seconds = frame_count / OUTPUT_SAMPLE_RATE_HZ
        print(f"decoded packets: {packet_count}")
        print(f"left samples:    {states[0].cursor_samples}")
        print(f"right samples:   {states[1].cursor_samples}")
        print(format_peak_report(states[0]))
        print(format_peak_report(states[1]))
        print(f"duration:        {duration_seconds:.3f} s")
        if info_comment is not None:
            print(f"wav comment:     {info_comment}")
        print(f"pcm output:      {pcm_path}")
        print(f"wav output:      {wav_path}")
    finally:
        for state in states:
            if not state.file.closed:
                state.file.close()
        for temp_path in (left_path, right_path):
            try:
                temp_path.unlink()
            except FileNotFoundError:
                pass


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Decode The Fly .rec audio recordings to .wav")
    parser.add_argument("input", type=Path, help="input .rec recording")
    parser.add_argument("-o", "--output", type=Path, help="output .wav path; defaults to input path with .wav extension")
    parser.add_argument("--pcm-output", type=Path, help="intermediate stereo .pcm path; defaults to input path with .pcm extension")
    parser.add_argument("--gap-threshold-ms", type=float, default=200.0, help="minimum timestamp gap to treat as silence; default: 200")
    parser.add_argument("--key", type=Path, help="optional .key file; when supplied, input must be AES-GCM encrypted")
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    input_path = args.input

    if not input_path.exists():
        print(f"error: input file does not exist: {input_path}", file=sys.stderr)
        return 2
    if input_path.suffix.lower() != ".rec":
        warn(f"input file extension is {input_path.suffix!r}; expected '.rec'")
    if args.gap_threshold_ms < 0:
        print("error: --gap-threshold-ms must be non-negative", file=sys.stderr)
        return 2

    wav_path = args.output if args.output is not None else input_path.with_suffix(".wav")
    pcm_path = args.pcm_output if args.pcm_output is not None else input_path.with_suffix(".pcm")

    resolved_input = input_path.resolve()
    if wav_path.resolve() == resolved_input:
        print(f"error: output WAV path would overwrite input: {wav_path}", file=sys.stderr)
        return 2
    if pcm_path.resolve() == resolved_input:
        print(f"error: intermediate PCM path would overwrite input: {pcm_path}", file=sys.stderr)
        return 2
    if wav_path.resolve() == pcm_path.resolve():
        print(f"error: output WAV path and intermediate PCM path are the same: {wav_path}", file=sys.stderr)
        return 2

    decrypt_temp_path: Optional[Path] = None

    try:
        decode_input_path = input_path
        if args.key is not None:
            key = sectools.read_key_file(args.key)
            decrypt_temp = tempfile.NamedTemporaryFile(prefix="thefly-decrypted-", suffix=".rec", delete=False)
            decrypt_temp_path = Path(decrypt_temp.name)
            decrypt_temp.close()

            packet_count = sectools.decrypt_recording_file(input_path, decrypt_temp_path, key)
            print(f"decrypted packets: {packet_count}")
            decode_input_path = decrypt_temp_path

        decode_recording(decode_input_path, pcm_path, wav_path, args.gap_threshold_ms)
    except OSError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    finally:
        if decrypt_temp_path is not None:
            try:
                decrypt_temp_path.unlink()
            except FileNotFoundError:
                pass

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
