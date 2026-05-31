#!/usr/bin/env python3
from __future__ import annotations

"""
Decrypt The Fly encrypted `.rec` recordings into ordinary WAV files.

Current secure recordings are not `file_packet_t` streams. They are 16 kHz,
16-bit, stereo WAV data encrypted in chunks:

* one encrypted 44-byte RIFF/WAVE header chunk
* zero or more encrypted 8192-byte PCM audio chunks

Each encrypted chunk is stored as nonce + ciphertext + AES-GCM tag, matching
`AudioFileRecorder` and `WavPlayback`.
"""

import argparse
import struct
import sys
import wave
from pathlib import Path
from typing import BinaryIO, Optional, Sequence


FILECRYPT_KEY_SIZE = 32
OUTPUT_SAMPLE_RATE_HZ = 16000
OUTPUT_CHANNELS = 2
SAMPLE_WIDTH_BYTES = 2
OUTPUT_BITS_PER_SAMPLE = SAMPLE_WIDTH_BYTES * 8
OUTPUT_FRAME_BYTES = OUTPUT_CHANNELS * SAMPLE_WIDTH_BYTES

WAV_RIFF_HEADER_LENGTH = 44
WAV_ENCRYPTED_CHUNK_NONCE_LENGTH = 12
WAV_ENCRYPTED_CHUNK_TAG_LENGTH = 16
WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH = 8192
WAV_ENCRYPTED_RIFF_HEADER_LENGTH = (
    WAV_ENCRYPTED_CHUNK_NONCE_LENGTH + WAV_RIFF_HEADER_LENGTH + WAV_ENCRYPTED_CHUNK_TAG_LENGTH
)
WAV_ENCRYPTED_AUDIO_CHUNK_LENGTH = (
    WAV_ENCRYPTED_CHUNK_NONCE_LENGTH
    + WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH
    + WAV_ENCRYPTED_CHUNK_TAG_LENGTH
)

PCM_COPY_FRAMES = 4096


class AudioDecryptorError(ValueError):
    pass


def warn(message: str) -> None:
    print(f"warning: {message}", file=sys.stderr)


def load_sectools():
    try:
        import sectools
    except ModuleNotFoundError as exc:
        if exc.name == "cryptography":
            raise AudioDecryptorError(
                "missing Python package 'cryptography'; install the same dependency used by scripts/sectools.py"
            ) from exc
        raise
    return sectools


def load_crypto():
    try:
        from cryptography.exceptions import InvalidTag
        from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    except ModuleNotFoundError as exc:
        if exc.name == "cryptography":
            raise AudioDecryptorError(
                "missing Python package 'cryptography'; install the same dependency used by scripts/sectools.py"
            ) from exc
        raise
    return AESGCM, InvalidTag


def read_exact(stream: BinaryIO, size: int, offset: int, description: str) -> bytes:
    data = stream.read(size)
    if len(data) != size:
        raise AudioDecryptorError(f"truncated {description} at byte {offset}; expected {size} bytes, got {len(data)}")
    return data


def decrypt_chunk(aesgcm, invalid_tag_type, packet: bytes, plaintext_size: int, offset: int, description: str) -> bytes:
    expected_size = WAV_ENCRYPTED_CHUNK_NONCE_LENGTH + plaintext_size + WAV_ENCRYPTED_CHUNK_TAG_LENGTH
    if len(packet) != expected_size:
        raise AudioDecryptorError(f"invalid {description} size {len(packet)}; expected {expected_size}")

    nonce = packet[:WAV_ENCRYPTED_CHUNK_NONCE_LENGTH]
    ciphertext = packet[WAV_ENCRYPTED_CHUNK_NONCE_LENGTH : WAV_ENCRYPTED_CHUNK_NONCE_LENGTH + plaintext_size]
    tag = packet[WAV_ENCRYPTED_CHUNK_NONCE_LENGTH + plaintext_size :]

    try:
        return aesgcm.decrypt(nonce, ciphertext + tag, None)
    except invalid_tag_type as exc:
        raise AudioDecryptorError(f"AES-GCM authentication failed for {description} at byte {offset}") from exc


def validate_recorder_wav_header(header: bytes) -> None:
    if len(header) != WAV_RIFF_HEADER_LENGTH:
        raise AudioDecryptorError(f"invalid decrypted WAV header size {len(header)}; expected {WAV_RIFF_HEADER_LENGTH}")
    if header[:4] != b"RIFF" or header[8:12] != b"WAVE" or header[12:16] != b"fmt " or header[36:40] != b"data":
        raise AudioDecryptorError("decrypted header is not a supported RIFF/WAVE header")

    fmt_size = struct.unpack_from("<I", header, 16)[0]
    audio_format, channels, sample_rate, byte_rate, block_align, bits_per_sample = struct.unpack_from("<HHIIHH", header, 20)
    if fmt_size != 16 or audio_format != 1:
        raise AudioDecryptorError("decrypted WAV header is not PCM")
    if channels != OUTPUT_CHANNELS:
        raise AudioDecryptorError(f"decrypted WAV header has {channels} channels; expected {OUTPUT_CHANNELS}")
    if sample_rate != OUTPUT_SAMPLE_RATE_HZ:
        raise AudioDecryptorError(f"decrypted WAV header has {sample_rate} Hz sample rate; expected {OUTPUT_SAMPLE_RATE_HZ}")
    if bits_per_sample != OUTPUT_BITS_PER_SAMPLE:
        raise AudioDecryptorError(
            f"decrypted WAV header has {bits_per_sample} bits per sample; expected {OUTPUT_BITS_PER_SAMPLE}"
        )
    if block_align != OUTPUT_FRAME_BYTES:
        raise AudioDecryptorError(f"decrypted WAV header has block align {block_align}; expected {OUTPUT_FRAME_BYTES}")
    expected_byte_rate = OUTPUT_SAMPLE_RATE_HZ * OUTPUT_FRAME_BYTES
    if byte_rate != expected_byte_rate:
        raise AudioDecryptorError(f"decrypted WAV header has byte rate {byte_rate}; expected {expected_byte_rate}")


def validate_plain_wav(wav: wave.Wave_read, input_path: Path) -> None:
    if wav.getcomptype() != "NONE":
        raise AudioDecryptorError(f"unsupported compressed WAV input: {input_path}")
    if wav.getnchannels() != OUTPUT_CHANNELS:
        raise AudioDecryptorError(f"input WAV has {wav.getnchannels()} channels; expected {OUTPUT_CHANNELS}")
    if wav.getsampwidth() != SAMPLE_WIDTH_BYTES:
        raise AudioDecryptorError(f"input WAV has {wav.getsampwidth()}-byte samples; expected {SAMPLE_WIDTH_BYTES}")
    if wav.getframerate() != OUTPUT_SAMPLE_RATE_HZ:
        raise AudioDecryptorError(f"input WAV has {wav.getframerate()} Hz sample rate; expected {OUTPUT_SAMPLE_RATE_HZ}")


def is_plain_wav_path(input_path: Path) -> bool:
    with input_path.open("rb") as stream:
        header = stream.read(12)
    return len(header) == 12 and header[:4] == b"RIFF" and header[8:12] == b"WAVE"


def configure_wav_output(wav: wave.Wave_write) -> None:
    wav.setnchannels(OUTPUT_CHANNELS)
    wav.setsampwidth(SAMPLE_WIDTH_BYTES)
    wav.setframerate(OUTPUT_SAMPLE_RATE_HZ)


def wrap_pcm_as_wav(pcm_path: Path, wav_path: Path) -> int:
    wav_path.parent.mkdir(parents=True, exist_ok=True)
    total_pcm_bytes = 0
    with pcm_path.open("rb") as pcm, wave.open(str(wav_path), "wb") as wav:
        configure_wav_output(wav)
        while True:
            data = pcm.read(64 * 1024)
            if not data:
                break
            if len(data) % OUTPUT_FRAME_BYTES:
                raise AudioDecryptorError(
                    f"PCM data length is not aligned to {OUTPUT_FRAME_BYTES}-byte stereo frames: {pcm_path}"
                )
            wav.writeframes(data)
            total_pcm_bytes += len(data)
    return total_pcm_bytes


def copy_plain_wav(input_path: Path, pcm_path: Path, wav_path: Path) -> tuple[int, int]:
    pcm_path.parent.mkdir(parents=True, exist_ok=True)
    wav_path.parent.mkdir(parents=True, exist_ok=True)

    total_pcm_bytes = 0
    with wave.open(str(input_path), "rb") as src:
        validate_plain_wav(src, input_path)
        with pcm_path.open("wb") as pcm, wave.open(str(wav_path), "wb") as dst:
            configure_wav_output(dst)
            while True:
                data = src.readframes(PCM_COPY_FRAMES)
                if not data:
                    break
                pcm.write(data)
                dst.writeframes(data)
                total_pcm_bytes += len(data)

    return total_pcm_bytes, 0


def decrypt_rec_to_pcm(input_path: Path, pcm_path: Path, key: bytes) -> tuple[int, int, int]:
    if len(key) != FILECRYPT_KEY_SIZE:
        raise AudioDecryptorError(f"AES-GCM filecrypt key must be {FILECRYPT_KEY_SIZE} bytes")

    file_size = input_path.stat().st_size
    if file_size < WAV_ENCRYPTED_RIFF_HEADER_LENGTH:
        raise AudioDecryptorError(
            f"encrypted recording is too small: {file_size} bytes; expected at least {WAV_ENCRYPTED_RIFF_HEADER_LENGTH}"
        )

    encrypted_audio_bytes = file_size - WAV_ENCRYPTED_RIFF_HEADER_LENGTH
    audio_chunks = encrypted_audio_bytes // WAV_ENCRYPTED_AUDIO_CHUNK_LENGTH
    trailing_bytes = encrypted_audio_bytes % WAV_ENCRYPTED_AUDIO_CHUNK_LENGTH
    if trailing_bytes:
        warn(f"ignoring {trailing_bytes} trailing bytes after the last complete encrypted audio chunk")

    AESGCM, InvalidTag = load_crypto()
    aesgcm = AESGCM(key)

    pcm_path.parent.mkdir(parents=True, exist_ok=True)
    total_pcm_bytes = 0
    with input_path.open("rb") as src, pcm_path.open("wb") as pcm:
        header_packet = read_exact(src, WAV_ENCRYPTED_RIFF_HEADER_LENGTH, 0, "encrypted WAV header")
        header = decrypt_chunk(aesgcm, InvalidTag, header_packet, WAV_RIFF_HEADER_LENGTH, 0, "encrypted WAV header")
        validate_recorder_wav_header(header)

        for chunk_index in range(audio_chunks):
            offset = WAV_ENCRYPTED_RIFF_HEADER_LENGTH + chunk_index * WAV_ENCRYPTED_AUDIO_CHUNK_LENGTH
            packet = read_exact(src, WAV_ENCRYPTED_AUDIO_CHUNK_LENGTH, offset, f"encrypted audio chunk {chunk_index}")
            plaintext = decrypt_chunk(
                aesgcm,
                InvalidTag,
                packet,
                WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH,
                offset,
                f"encrypted audio chunk {chunk_index}",
            )
            pcm.write(plaintext)
            total_pcm_bytes += len(plaintext)

    return total_pcm_bytes, audio_chunks, trailing_bytes


def decode_recording(
    input_path: Path,
    pcm_path: Path,
    wav_path: Path,
    gap_threshold_ms: float,
    key: Optional[bytes] = None,
) -> None:
    del gap_threshold_ms  # Kept for CLI/API compatibility with the former packet decoder.

    if key is None:
        if not is_plain_wav_path(input_path):
            raise AudioDecryptorError(f"encrypted recording requires --key: {input_path}")
        total_pcm_bytes, audio_chunks = copy_plain_wav(input_path, pcm_path, wav_path)
        trailing_bytes = 0
        print("copied plain WAV input")
    else:
        total_pcm_bytes, audio_chunks, trailing_bytes = decrypt_rec_to_pcm(input_path, pcm_path, key)
        total_pcm_bytes = wrap_pcm_as_wav(pcm_path, wav_path)
        print(f"decrypted audio chunks: {audio_chunks}")

    frame_count = total_pcm_bytes // OUTPUT_FRAME_BYTES
    duration_seconds = frame_count / OUTPUT_SAMPLE_RATE_HZ
    print(f"pcm bytes:    {total_pcm_bytes}")
    print(f"frames:       {frame_count}")
    print(f"duration:     {duration_seconds:.3f} s")
    if trailing_bytes:
        print(f"trailing:     {trailing_bytes} ignored bytes")
    print(f"pcm output:   {pcm_path}")
    print(f"wav output:   {wav_path}")


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Decrypt The Fly .rec audio recordings to .wav")
    parser.add_argument("input", type=Path, help="input .rec recording")
    parser.add_argument("-o", "--output", type=Path, help="output .wav path; defaults to input path with .wav extension")
    parser.add_argument("--pcm-output", type=Path, help="intermediate stereo .pcm path; defaults to input path with .pcm extension")
    parser.add_argument(
        "--gap-threshold-ms",
        type=float,
        default=200.0,
        help="accepted for compatibility with older packet recordings; unused for chunked WAV recordings",
    )
    parser.add_argument("--key", type=Path, help="optional .key file for encrypted .rec files")
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

    try:
        key = None
        if args.key is not None:
            sectools = load_sectools()
            key = sectools.read_key_file(args.key)

        decode_recording(input_path, pcm_path, wav_path, args.gap_threshold_ms, key)
    except OSError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
