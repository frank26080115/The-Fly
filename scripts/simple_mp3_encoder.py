#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Optional, Sequence


OUTPUT_SAMPLE_RATE_HZ = 16000
OUTPUT_CHANNELS = 2
OUTPUT_SAMPLE_FORMAT = "s16p"
MP3_BIT_RATE_KBPS = 64


class SimpleMp3EncoderError(ValueError):
    pass


def output_path_for_input(input_path: Path) -> Path:
    return input_path.with_suffix(".e.mp3")


def ffmpeg_command(ffmpeg: str, input_path: Path, output_path: Path) -> list[str]:
    return [
        ffmpeg,
        "-hide_banner",
        "-loglevel",
        "error",
        "-nostdin",
        "-y",
        "-i",
        str(input_path),
        "-vn",
        "-map",
        "0:a:0",
        "-ac",
        str(OUTPUT_CHANNELS),
        "-ar",
        str(OUTPUT_SAMPLE_RATE_HZ),
        "-sample_fmt",
        OUTPUT_SAMPLE_FORMAT,
        "-codec:a",
        "libmp3lame",
        "-b:a",
        f"{MP3_BIT_RATE_KBPS}k",
        "-compression_level",
        "9",
        "-reservoir",
        "0",
        "-write_xing",
        "0",
        "-id3v2_version",
        "0",
        "-write_id3v1",
        "0",
        "-f",
        "mp3",
        str(output_path),
    ]


def encode_mp3(input_path: Path, output_path: Path, ffmpeg: str) -> None:
    temp_file = tempfile.NamedTemporaryFile(
        prefix=f".{output_path.name}.",
        suffix=".tmp",
        dir=output_path.parent,
        delete=False,
    )
    temp_path = Path(temp_file.name)
    temp_file.close()

    command = ffmpeg_command(ffmpeg, input_path, temp_path)
    try:
        completed = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        if completed.returncode != 0:
            message = completed.stderr.decode("utf-8", errors="replace").strip()
            if not message:
                message = f"ffmpeg exited with status {completed.returncode}"
            raise SimpleMp3EncoderError(message)

        temp_path.replace(output_path)
    except Exception:
        try:
            temp_path.unlink()
        except FileNotFoundError:
            pass
        raise


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Re-encode an audio file as 16 kHz stereo 64 kbps CBR MP3"
    )
    parser.add_argument("input", type=Path, help="input audio file; any format ffmpeg can decode")
    parser.add_argument("--ffmpeg", default="ffmpeg", help="ffmpeg executable path; default: ffmpeg")
    parser.add_argument("-f", "--force", action="store_true", help="overwrite the .e.mp3 output if it exists")
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    input_path = args.input.expanduser()
    output_path = output_path_for_input(input_path)

    if not input_path.exists():
        print(f"error: input file does not exist: {input_path}", file=sys.stderr)
        return 2
    if not input_path.is_file():
        print(f"error: input path is not a file: {input_path}", file=sys.stderr)
        return 2
    if shutil.which(args.ffmpeg) is None and not Path(args.ffmpeg).exists():
        print(f"error: ffmpeg executable not found: {args.ffmpeg}", file=sys.stderr)
        return 2
    if input_path.resolve() == output_path.resolve():
        print(f"error: output path would overwrite input: {output_path}", file=sys.stderr)
        return 2
    if output_path.exists() and not args.force:
        print(f"error: output file exists; use --force to overwrite: {output_path}", file=sys.stderr)
        return 2

    try:
        encode_mp3(input_path, output_path, args.ffmpeg)
    except (OSError, SimpleMp3EncoderError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"wrote {output_path}")
    print(f"format: 16 kHz, {OUTPUT_CHANNELS} channel, {MP3_BIT_RATE_KBPS} kbps CBR MP3")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
