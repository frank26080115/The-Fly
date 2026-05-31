#!/usr/bin/env python3
from __future__ import annotations

import argparse
import secrets
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import BinaryIO, Optional, Sequence


OUTPUT_SAMPLE_RATE_HZ = 16000
OUTPUT_CHANNELS = 2
OUTPUT_BITS_PER_SAMPLE = 16
OUTPUT_SAMPLE_WIDTH_BYTES = OUTPUT_BITS_PER_SAMPLE // 8
OUTPUT_FRAME_BYTES = OUTPUT_CHANNELS * OUTPUT_SAMPLE_WIDTH_BYTES

WAV_RIFF_HEADER_LENGTH = 44
WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH = 8192
WAV_PLACEHOLDER_DATA_BYTES = 0x7FFFFFFF

READ_SIZE = 64 * 1024


class AudioFileEncryptorError(ValueError):
    pass


def load_sectools():
    try:
        import sectools
    except ModuleNotFoundError as exc:
        if exc.name == "cryptography":
            raise AudioFileEncryptorError(
                "missing Python package 'cryptography'; install the same dependency used by scripts/sectools.py"
            ) from exc
        raise
    return sectools


class RecorderEncryptor:
    def __init__(self, key: bytes, key_size: int) -> None:
        try:
            from cryptography.hazmat.primitives.ciphers.aead import AESGCM
        except ModuleNotFoundError as exc:
            if exc.name == "cryptography":
                raise AudioFileEncryptorError(
                    "missing Python package 'cryptography'; install the same dependency used by scripts/sectools.py"
                ) from exc
            raise

        if len(key) != key_size:
            raise AudioFileEncryptorError(f"AES-GCM filecrypt key must be {key_size} bytes")
        self._aesgcm = AESGCM(key)
        self._sequence = 0

    def encrypt_chunk(self, plaintext: bytes) -> bytes:
        nonce = secrets.token_bytes(8) + self._sequence.to_bytes(4, "big")
        self._sequence = (self._sequence + 1) & 0xFFFFFFFF
        return nonce + self._aesgcm.encrypt(nonce, plaintext, None)


def build_placeholder_wav_header() -> bytes:
    byte_rate = OUTPUT_SAMPLE_RATE_HZ * OUTPUT_CHANNELS * OUTPUT_SAMPLE_WIDTH_BYTES
    block_align = OUTPUT_CHANNELS * OUTPUT_SAMPLE_WIDTH_BYTES

    return struct.pack(
        "<4sI4s4sIHHIIHH4sI",
        b"RIFF",
        WAV_PLACEHOLDER_DATA_BYTES + 36,
        b"WAVE",
        b"fmt ",
        16,
        1,
        OUTPUT_CHANNELS,
        OUTPUT_SAMPLE_RATE_HZ,
        byte_rate,
        block_align,
        OUTPUT_BITS_PER_SAMPLE,
        b"data",
        WAV_PLACEHOLDER_DATA_BYTES,
    )


def ffmpeg_command(ffmpeg: str, input_path: Path) -> list[str]:
    return [
        ffmpeg,
        "-hide_banner",
        "-loglevel",
        "error",
        "-nostdin",
        "-i",
        str(input_path),
        "-vn",
        "-map",
        "0:a:0",
        "-ac",
        str(OUTPUT_CHANNELS),
        "-ar",
        str(OUTPUT_SAMPLE_RATE_HZ),
        "-f",
        "s16le",
        "-acodec",
        "pcm_s16le",
        "-",
    ]


def write_encrypted_rec(pcm_stream: BinaryIO, output_stream: BinaryIO, key: bytes, key_size: int) -> tuple[int, int]:
    encryptor = RecorderEncryptor(key, key_size)
    header = build_placeholder_wav_header()
    if len(header) != WAV_RIFF_HEADER_LENGTH:
        raise AssertionError("unexpected WAV header length")

    output_stream.write(encryptor.encrypt_chunk(header))

    pending = bytearray()
    total_pcm_bytes = 0
    audio_chunks = 0

    while True:
        data = pcm_stream.read(READ_SIZE)
        if not data:
            break

        pending += data
        total_pcm_bytes += len(data)

        while len(pending) >= WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH:
            chunk = bytes(pending[:WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH])
            del pending[:WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH]
            output_stream.write(encryptor.encrypt_chunk(chunk))
            audio_chunks += 1

    if total_pcm_bytes % OUTPUT_FRAME_BYTES:
        raise AudioFileEncryptorError(
            f"converted PCM length {total_pcm_bytes} is not a whole {OUTPUT_FRAME_BYTES}-byte stereo frame"
        )

    if pending:
        pending += b"\x00" * (WAV_ENCRYPTED_AUDIO_PLAINTEXT_LENGTH - len(pending))
        output_stream.write(encryptor.encrypt_chunk(bytes(pending)))
        audio_chunks += 1

    return total_pcm_bytes, audio_chunks


def convert_audio_to_rec(input_path: Path, output_path: Path, key: bytes, key_size: int, ffmpeg: str) -> tuple[int, int]:
    command = ffmpeg_command(ffmpeg, input_path)
    temp_file = tempfile.NamedTemporaryFile(
        prefix=f".{output_path.name}.",
        suffix=".tmp",
        dir=output_path.parent,
        delete=False,
    )
    temp_path = Path(temp_file.name)
    temp_file.close()

    process: Optional[subprocess.Popen[bytes]] = None
    try:
        with tempfile.TemporaryFile() as stderr_stream:
            process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=stderr_stream)
            if process.stdout is None:
                raise AudioFileEncryptorError("ffmpeg stdout pipe was not created")

            with temp_path.open("wb") as output_stream:
                total_pcm_bytes, audio_chunks = write_encrypted_rec(process.stdout, output_stream, key, key_size)

            return_code = process.wait()
            stderr_stream.seek(0)
            stderr = stderr_stream.read()
            if return_code != 0:
                message = stderr.decode("utf-8", errors="replace").strip()
                if not message:
                    message = f"ffmpeg exited with status {return_code}"
                raise AudioFileEncryptorError(message)

        temp_path.replace(output_path)
        return total_pcm_bytes, audio_chunks
    except Exception:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait()
        try:
            temp_path.unlink()
        except FileNotFoundError:
            pass
        raise


def load_filecrypt_key(sectools, key_file: Optional[Path], password: Optional[str]) -> bytes:
    if key_file is not None:
        return sectools.read_key_file(key_file.expanduser())
    if password is None:
        raise AudioFileEncryptorError("either --key or --password is required")
    return sectools.derive_filecrypt_key(password)


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert an audio file into an AudioFileRecorder-compatible encrypted .rec file"
    )
    parser.add_argument("input", type=Path, help="input audio file")
    key_group = parser.add_mutually_exclusive_group(required=True)
    key_group.add_argument("--key", type=Path, help="filecrypt key file")
    key_group.add_argument("--password", help="derive the filecrypt key from this password without saving it")
    parser.add_argument("--ffmpeg", default="ffmpeg", help="ffmpeg executable path; default: ffmpeg")
    parser.add_argument("-f", "--force", action="store_true", help="overwrite the output .rec file if it already exists")
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    input_path = args.input.expanduser()
    output_path = input_path.with_suffix(".rec")

    if shutil.which(args.ffmpeg) is None and not Path(args.ffmpeg).exists():
        print(f"error: ffmpeg executable not found: {args.ffmpeg}", file=sys.stderr)
        return 2

    if not input_path.exists():
        print(f"error: input file does not exist: {input_path}", file=sys.stderr)
        return 2
    if not input_path.is_file():
        print(f"error: input path is not a file: {input_path}", file=sys.stderr)
        return 2
    if input_path.resolve() == output_path.resolve():
        print(f"error: output path would overwrite input: {output_path}", file=sys.stderr)
        return 2
    if output_path.exists() and not args.force:
        print(f"error: output file exists; use --force to overwrite: {output_path}", file=sys.stderr)
        return 2

    try:
        sectools = load_sectools()
        key = load_filecrypt_key(sectools, args.key, args.password)
        total_pcm_bytes, audio_chunks = convert_audio_to_rec(
            input_path,
            output_path,
            key,
            sectools.FILECRYPT_KEY_SIZE,
            args.ffmpeg,
        )
    except (OSError, AudioFileEncryptorError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    except Exception as exc:
        if exc.__class__.__name__ != "SecurityToolError":
            raise
        print(f"error: {exc}", file=sys.stderr)
        return 1

    duration_seconds = total_pcm_bytes / (OUTPUT_SAMPLE_RATE_HZ * OUTPUT_FRAME_BYTES)
    print(f"wrote {output_path}")
    print(f"pcm bytes:    {total_pcm_bytes}")
    print(f"audio chunks: {audio_chunks}")
    print(f"duration:     {duration_seconds:.3f} s")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
