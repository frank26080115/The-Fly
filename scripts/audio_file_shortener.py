#!/usr/bin/env python3
from __future__ import annotations

import argparse
import math
import shutil
import subprocess
import sys
import tempfile
import wave
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Optional, Sequence, Union

try:
    import audioop
except ModuleNotFoundError:
    audioop = None


DEFAULT_SILENCE_LEVEL_DB = -42.0
DEFAULT_MAX_SILENCE_SECONDS = 3.0
DEFAULT_MIN_SILENCE_SECONDS = 0.30
DEFAULT_WINDOW_SECONDS = 0.05
SPLIT_THRESHOLD_SECONDS = 22.0 * 60.0
MIN_SECTION_SECONDS = 12.0 * 60.0
MAX_SECTION_SECONDS = 18.0 * 60.0
SPLIT_SEARCH_WINDOW_SECONDS = 2.0 * 60.0
SAMPLE_WIDTH_BYTES = 2
MAX_INT16 = 32767


class AudioShortenerError(ValueError):
    pass


@dataclass(frozen=True)
class SilenceChunk:
    start: float
    stop: float

    @property
    def duration(self) -> float:
        return max(0.0, self.stop - self.start)


@dataclass(frozen=True)
class PcmAudio:
    sample_rate: int
    channels: int
    pcm: bytes

    @property
    def frame_bytes(self) -> int:
        return self.channels * SAMPLE_WIDTH_BYTES

    @property
    def frame_count(self) -> int:
        return len(self.pcm) // self.frame_bytes

    @property
    def duration(self) -> float:
        return self.frame_count / self.sample_rate if self.sample_rate > 0 else 0.0


AudioShortenerResult = Union[Path, list[Path]]


def shorten_audio_file(
    input_path: Path,
    output_path: Optional[Path] = None,
    silence_level_db: float = DEFAULT_SILENCE_LEVEL_DB,
    ffmpeg: str = "ffmpeg",
) -> AudioShortenerResult:
    input_path = Path(input_path)
    validate_input_path(input_path)

    print_progress(f"audio shortener: decoding {input_path}")
    audio = decode_audio_file(input_path, ffmpeg)
    print_progress(
        "audio shortener: decoded "
        f"{format_duration(audio.duration)} audio, {audio.channels} channel(s), {audio.sample_rate} Hz"
    )
    if audio.duration <= SPLIT_THRESHOLD_SECONDS:
        print_progress(
            "audio shortener: under "
            f"{format_duration(SPLIT_THRESHOLD_SECONDS)} split threshold; using original file"
        )
        return input_path

    print_progress(f"audio shortener: analyzing silence at {silence_level_db:g} dBFS")
    silence_chunks = analyze_silence_chunks(audio, silence_level_db=silence_level_db)
    print_progress(f"audio shortener: found {len(silence_chunks)} silence chunk(s)")
    sections = choose_section_ranges(audio, silence_chunks)
    output_base = default_output_base(input_path, output_path)
    print_progress(f"audio shortener: splitting into {len(sections)} part(s)")
    for split_index, section in enumerate(sections[:-1], start=1):
        print_progress(f"audio shortener: split {split_index} at {format_duration(frames_to_seconds(audio, section[1]))}")

    output_paths: list[Path] = []
    for index, section in enumerate(sections, start=1):
        part_path = part_output_path(output_base, index)
        raw_duration = frames_to_seconds(audio, section[1] - section[0])
        print_progress(
            f"audio shortener: writing part {index}/{len(sections)} "
            f"from {format_duration(frames_to_seconds(audio, section[0]))} "
            f"to {format_duration(frames_to_seconds(audio, section[1]))} "
            f"({format_duration(raw_duration)} before silence trimming)"
        )
        write_processed_section(audio, section[0], section[1], silence_chunks, part_path)
        print_progress(
            f"audio shortener: wrote {part_path} "
            f"({format_duration(audio_duration_seconds(part_path))} after silence trimming)"
        )
        output_paths.append(part_path)

    return output_paths


def audio_duration_seconds(input_path: Path) -> float:
    input_path = Path(input_path)
    with wave.open(str(input_path), "rb") as wav:
        frames = wav.getnframes()
        sample_rate = wav.getframerate()
        return frames / sample_rate if sample_rate > 0 else 0.0


def analyze_silence_chunks(
    audio: PcmAudio,
    silence_level_db: float = DEFAULT_SILENCE_LEVEL_DB,
    min_silence_seconds: float = DEFAULT_MIN_SILENCE_SECONDS,
    window_seconds: float = DEFAULT_WINDOW_SECONDS,
) -> list[SilenceChunk]:
    if audio.sample_rate <= 0 or audio.channels <= 0:
        raise AudioShortenerError("decoded audio has invalid sample rate or channel count")

    threshold = silence_threshold(silence_level_db)
    window_frames = max(1, int(round(audio.sample_rate * window_seconds)))
    min_silence_frames = max(1, int(round(audio.sample_rate * min_silence_seconds)))
    chunks: list[SilenceChunk] = []
    silent_start: Optional[int] = None

    for start_frame in range(0, audio.frame_count, window_frames):
        stop_frame = min(audio.frame_count, start_frame + window_frames)
        rms = rms_for_frames(audio, start_frame, stop_frame)
        is_silent = rms <= threshold

        if is_silent and silent_start is None:
            silent_start = start_frame
        elif not is_silent and silent_start is not None:
            if start_frame - silent_start >= min_silence_frames:
                chunks.append(frame_chunk(audio, silent_start, start_frame))
            silent_start = None

    if silent_start is not None and audio.frame_count - silent_start >= min_silence_frames:
        chunks.append(frame_chunk(audio, silent_start, audio.frame_count))

    return chunks


def decode_audio_file(input_path: Path, ffmpeg: str = "ffmpeg") -> PcmAudio:
    if is_wave_file(input_path):
        try:
            return read_wave_pcm(input_path)
        except AudioShortenerError:
            pass

    ffmpeg_path = resolve_executable(ffmpeg)
    if not ffmpeg_path:
        raise AudioShortenerError(
            f"ffmpeg executable not found: {ffmpeg}; install ffmpeg or pass --ffmpeg for non-16-bit-PCM WAV inputs"
        )

    print_progress(f"audio shortener: decoding through ffmpeg: {input_path}")
    with tempfile.TemporaryDirectory(prefix="thefly-shortener-") as temp_dir:
        temp_wav = Path(temp_dir) / "decoded.wav"
        command = [
            ffmpeg_path,
            "-hide_banner",
            "-loglevel",
            "error",
            "-y",
            "-i",
            str(input_path),
            "-vn",
            "-acodec",
            "pcm_s16le",
            str(temp_wav),
        ]
        completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if completed.returncode != 0:
            detail = completed.stderr.strip() or f"exit status {completed.returncode}"
            raise AudioShortenerError(f"ffmpeg failed to decode {input_path}: {detail}")
        return read_wave_pcm(temp_wav)


def read_wave_pcm(input_path: Path) -> PcmAudio:
    with wave.open(str(input_path), "rb") as wav:
        if wav.getcomptype() != "NONE":
            raise AudioShortenerError(f"unsupported compressed WAV input: {input_path}")
        if wav.getsampwidth() != SAMPLE_WIDTH_BYTES:
            raise AudioShortenerError(
                f"input WAV has {wav.getsampwidth()}-byte samples; expected {SAMPLE_WIDTH_BYTES}"
            )
        channels = wav.getnchannels()
        sample_rate = wav.getframerate()
        if channels <= 0:
            raise AudioShortenerError(f"input WAV has invalid channel count: {channels}")
        if sample_rate <= 0:
            raise AudioShortenerError(f"input WAV has invalid sample rate: {sample_rate}")
        pcm = wav.readframes(wav.getnframes())

    frame_bytes = channels * SAMPLE_WIDTH_BYTES
    if len(pcm) % frame_bytes:
        raise AudioShortenerError(f"decoded PCM byte count is not aligned to {frame_bytes}-byte frames")
    return PcmAudio(sample_rate=sample_rate, channels=channels, pcm=pcm)


def choose_section_ranges(audio: PcmAudio, silence_chunks: Sequence[SilenceChunk]) -> list[tuple[int, int]]:
    total_frames = audio.frame_count
    if total_frames <= 0:
        return []

    part_count = max(2, math.ceil(audio.duration / MAX_SECTION_SECONDS))
    split_frames: list[int] = []
    previous = 0

    for split_index in range(1, part_count):
        remaining_parts = part_count - split_index
        rough = round(total_frames * split_index / part_count)
        lower, upper = split_search_bounds(audio, rough, previous, remaining_parts)
        split = best_split_frame(audio, silence_chunks, rough, lower, upper)
        split = min(max(split, previous + 1), total_frames - remaining_parts)
        split_frames.append(split)
        previous = split

    ranges: list[tuple[int, int]] = []
    start = 0
    for split in split_frames:
        ranges.append((start, split))
        start = split
    ranges.append((start, total_frames))
    return ranges


def split_search_bounds(audio: PcmAudio, rough: int, previous: int, remaining_parts: int) -> tuple[int, int]:
    total = audio.frame_count
    window = seconds_to_frames(audio, SPLIT_SEARCH_WINDOW_SECONDS)
    min_section = seconds_to_frames(audio, MIN_SECTION_SECONDS)
    max_section = seconds_to_frames(audio, MAX_SECTION_SECONDS)

    lower = max(rough - window, previous + min_section, total - remaining_parts * max_section)
    upper = min(rough + window, previous + max_section, total - remaining_parts * min_section)

    if lower <= upper:
        return lower, upper

    relaxed_lower = max(previous + 1, rough - window)
    relaxed_upper = min(total - remaining_parts, rough + window)
    if relaxed_lower <= relaxed_upper:
        return relaxed_lower, relaxed_upper

    clamped = min(max(rough, previous + 1), total - remaining_parts)
    return clamped, clamped


def best_split_frame(
    audio: PcmAudio,
    silence_chunks: Sequence[SilenceChunk],
    rough: int,
    lower: int,
    upper: int,
) -> int:
    best_start = 0
    best_stop = 0
    best_duration = -1
    best_distance = None

    for chunk in silence_chunks:
        start = max(seconds_to_frames(audio, chunk.start), lower)
        stop = min(seconds_to_frames(audio, chunk.stop), upper)
        if stop <= start:
            continue

        duration = stop - start
        center = (start + stop) // 2
        distance = abs(center - rough)
        if duration > best_duration or (duration == best_duration and (best_distance is None or distance < best_distance)):
            best_start = start
            best_stop = stop
            best_duration = duration
            best_distance = distance

    if best_duration >= 0:
        return (best_start + best_stop) // 2
    return min(max(rough, lower), upper)


def write_processed_section(
    audio: PcmAudio,
    section_start: int,
    section_stop: int,
    silence_chunks: Sequence[SilenceChunk],
    output_path: Path,
    max_silence_seconds: float = DEFAULT_MAX_SILENCE_SECONDS,
) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    keep_ranges = section_keep_ranges(audio, section_start, section_stop, silence_chunks, max_silence_seconds)

    with wave.open(str(output_path), "wb") as wav:
        wav.setnchannels(audio.channels)
        wav.setsampwidth(SAMPLE_WIDTH_BYTES)
        wav.setframerate(audio.sample_rate)
        for start, stop in keep_ranges:
            if stop <= start:
                continue
            wav.writeframes(frame_bytes(audio, start, stop))


def section_keep_ranges(
    audio: PcmAudio,
    section_start: int,
    section_stop: int,
    silence_chunks: Sequence[SilenceChunk],
    max_silence_seconds: float,
) -> list[tuple[int, int]]:
    max_silence_frames = max(1, seconds_to_frames(audio, max_silence_seconds))
    removals: list[tuple[int, int]] = []

    for chunk in silence_chunks:
        silence_start = max(seconds_to_frames(audio, chunk.start), section_start)
        silence_stop = min(seconds_to_frames(audio, chunk.stop), section_stop)
        silence_frames = silence_stop - silence_start
        if silence_frames <= max_silence_frames:
            continue

        if silence_start <= section_start:
            remove_start = section_start
            remove_stop = silence_stop - max_silence_frames
        else:
            remove_start = silence_start + max_silence_frames
            remove_stop = silence_stop

        if remove_stop > remove_start:
            removals.append((remove_start, remove_stop))

    keep_ranges: list[tuple[int, int]] = []
    cursor = section_start
    for remove_start, remove_stop in sorted(removals):
        if remove_start > cursor:
            keep_ranges.append((cursor, remove_start))
        cursor = max(cursor, remove_stop)

    if cursor < section_stop:
        keep_ranges.append((cursor, section_stop))

    return keep_ranges


def frame_bytes(audio: PcmAudio, start_frame: int, stop_frame: int) -> bytes:
    start = start_frame * audio.frame_bytes
    stop = stop_frame * audio.frame_bytes
    return audio.pcm[start:stop]


def rms_for_frames(audio: PcmAudio, start_frame: int, stop_frame: int) -> int:
    block = frame_bytes(audio, start_frame, stop_frame)
    if not block:
        return 0
    if audioop is not None:
        return int(audioop.rms(block, SAMPLE_WIDTH_BYTES))

    sample_count = len(block) // SAMPLE_WIDTH_BYTES
    if sample_count == 0:
        return 0

    total = 0
    for index in range(0, len(block), SAMPLE_WIDTH_BYTES):
        value = int.from_bytes(block[index : index + SAMPLE_WIDTH_BYTES], "little", signed=True)
        total += value * value
    return int(math.sqrt(total / sample_count))


def silence_threshold(silence_level_db: float) -> int:
    if silence_level_db >= 0:
        raise AudioShortenerError("--silence-level-db must be negative dBFS")
    return max(0, int(round(MAX_INT16 * (10.0 ** (silence_level_db / 20.0)))))


def frame_chunk(audio: PcmAudio, start_frame: int, stop_frame: int) -> SilenceChunk:
    return SilenceChunk(start=frames_to_seconds(audio, start_frame), stop=frames_to_seconds(audio, stop_frame))


def frames_to_seconds(audio: PcmAudio, frame: int) -> float:
    return frame / audio.sample_rate


def seconds_to_frames(audio: PcmAudio, seconds: float) -> int:
    return int(round(seconds * audio.sample_rate))


def default_output_base(input_path: Path, output_path: Optional[Path]) -> Path:
    if output_path is not None:
        return Path(output_path)
    return input_path.with_suffix(".wav")


def part_output_path(output_base: Path, index: int) -> Path:
    return output_base.with_name(f"{output_base.stem}.part{index}.wav")


def is_wave_file(input_path: Path) -> bool:
    try:
        with input_path.open("rb") as stream:
            header = stream.read(12)
    except OSError:
        return False
    return len(header) == 12 and header[:4] == b"RIFF" and header[8:12] == b"WAVE"


def resolve_executable(executable: str) -> Optional[str]:
    found = shutil.which(executable)
    if found:
        return found
    path = Path(executable)
    if path.exists():
        return str(path)
    return None


def validate_input_path(input_path: Path) -> None:
    if not input_path.exists():
        raise AudioShortenerError(f"input file does not exist: {input_path}")
    if not input_path.is_file():
        raise AudioShortenerError(f"input path is not a file: {input_path}")


def print_progress(message: str) -> None:
    print(f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] {message}", file=sys.stderr, flush=True)


def format_duration(seconds: float) -> str:
    seconds = max(0.0, float(seconds))
    total = int(round(seconds))
    hours = total // 3600
    minutes = (total % 3600) // 60
    secs = total % 60
    if hours:
        return f"{hours:d}:{minutes:02d}:{secs:02d}"
    return f"{minutes:d}:{secs:02d}"


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Split long audio files and cap long silence spans")
    parser.add_argument("input", type=Path, help="input audio file")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="output base path; parts are written as <base>.partN.wav; defaults beside input",
    )
    parser.add_argument(
        "--silence-level-db",
        type=float,
        default=DEFAULT_SILENCE_LEVEL_DB,
        help=f"silence threshold in dBFS; default: {DEFAULT_SILENCE_LEVEL_DB:g}",
    )
    parser.add_argument("--ffmpeg", default="ffmpeg", help="ffmpeg executable path; default: ffmpeg")
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)

    try:
        result = shorten_audio_file(
            args.input,
            output_path=args.output,
            silence_level_db=args.silence_level_db,
            ffmpeg=args.ffmpeg,
        )
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if isinstance(result, Path):
        print(f"audio under split threshold: {result}")
    else:
        for path in result:
            print(f"audio part: {path}")
    return 0


__all__ = [
    "AudioShortenerError",
    "AudioShortenerResult",
    "DEFAULT_SILENCE_LEVEL_DB",
    "SilenceChunk",
    "analyze_silence_chunks",
    "audio_duration_seconds",
    "decode_audio_file",
    "shorten_audio_file",
]


if __name__ == "__main__":
    raise SystemExit(main())
