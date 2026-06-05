#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Optional, Sequence

from audio_file_shortener import audio_duration_seconds, shorten_audio_file
from models import (
    DEFAULT_TRANSCRIPTION_MODEL,
    ModelError,
    add_recording_metadata,
    create_model,
    default_prompt_from_filename,
    default_transcription_output_path,
)


DEFAULT_MODEL = DEFAULT_TRANSCRIPTION_MODEL
DEFAULT_API_URL = "https://api.openai.com/v1/audio/transcriptions"


class TranscriptionError(ModelError):
    pass


def default_output_path(input_path: Path) -> Path:
    return default_transcription_output_path(input_path)


def transcribe_audio_file(
    input_path: Path,
    model: str,
    response_format: Optional[str],
    language: Optional[str],
    prompt: Optional[str],
    api_url: Optional[str],
    timeout: float,
    chunking_strategy: Optional[str] = None,
) -> dict[str, Any]:
    try:
        shortened = shorten_audio_file(input_path)
        audio_paths = shortened if isinstance(shortened, list) else [shortened]
        transcriber = create_model(model, base_url=api_url, timeout=timeout)
        results = [
            transcriber._transcribe_audio(path, response_format, language, prompt, chunking_strategy)
            for path in audio_paths
        ]
        if len(results) == 1:
            return results[0]
        return combine_part_transcriptions(input_path, audio_paths, results)
    except (OSError, ValueError) as exc:
        raise TranscriptionError(str(exc)) from exc


def combine_part_transcriptions(
    original_path: Path,
    audio_paths: Sequence[Path],
    results: Sequence[dict[str, Any]],
) -> dict[str, Any]:
    text_parts: list[str] = []
    segments: list[dict[str, Any]] = []
    parts: list[dict[str, Any]] = []
    offset_seconds = 0.0

    for index, (audio_path, result) in enumerate(zip(audio_paths, results), start=1):
        duration = safe_audio_duration_seconds(audio_path)
        part_text = transcription_text(result)
        if part_text:
            text_parts.append(part_text)

        part_record = {
            "index": index,
            "audio_path": str(audio_path),
            "duration": duration,
            "transcription": result,
        }
        parts.append(part_record)

        for segment in transcription_segments(result):
            adjusted = dict(segment)
            adjusted["part_index"] = index
            adjusted["part_audio_path"] = str(audio_path)
            adjust_segment_time(adjusted, "start", offset_seconds)
            adjust_segment_time(adjusted, "end", offset_seconds)
            segments.append(adjusted)

        offset_seconds += duration

    combined: dict[str, Any] = {
        "text": "\n\n".join(text_parts),
        "parts": parts,
        "source_audio_path": str(original_path),
        "source_audio_parts": [str(path) for path in audio_paths],
    }
    if segments:
        combined["segments"] = segments
    return combined


def transcription_text(result: dict[str, Any]) -> str:
    text = result.get("text")
    if isinstance(text, str) and text.strip():
        return text.strip()

    segment_text = [str(segment.get("text") or "").strip() for segment in transcription_segments(result)]
    return " ".join(text for text in segment_text if text)


def transcription_segments(result: dict[str, Any]) -> list[dict[str, Any]]:
    segments = result.get("segments")
    if not isinstance(segments, list):
        return []
    return [segment for segment in segments if isinstance(segment, dict)]


def adjust_segment_time(segment: dict[str, Any], key: str, offset_seconds: float) -> None:
    value = segment.get(key)
    if isinstance(value, (int, float)):
        segment[key] = value + offset_seconds


def safe_audio_duration_seconds(audio_path: Path) -> float:
    try:
        return audio_duration_seconds(audio_path)
    except (OSError, ValueError):
        return 0.0


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Transcribe a The Fly audio recording")
    parser.add_argument("input", type=Path, help="input audio file")
    parser.add_argument("-o", "--output", type=Path, help="output .trans.json path; defaults beside input")
    parser.add_argument("--model", default=DEFAULT_MODEL, help=f"transcription model; default: {DEFAULT_MODEL}")
    parser.add_argument(
        "--response-format",
        choices=("json", "verbose_json", "diarized_json"),
        default=None,
        help="transcription response format to request; defaults by model",
    )
    parser.add_argument("--language", help="optional input language hint, such as en")
    parser.add_argument("--prompt", help="optional transcription prompt/context")
    parser.add_argument("--chunking-strategy", help="optional chunking strategy, such as auto")
    parser.add_argument("--base-url", help="OpenAI-compatible base URL; defaults by model")
    parser.add_argument("--api-url", dest="base_url", help=argparse.SUPPRESS)
    parser.add_argument("--timeout", type=float, default=600.0, help="API request timeout in seconds; default: 600")
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    input_path: Path = args.input
    output_path: Path = args.output if args.output is not None else default_output_path(input_path)

    if args.timeout <= 0:
        print("error: --timeout must be positive", file=sys.stderr)
        return 2

    try:
        result = transcribe_audio_file(
            input_path=input_path,
            model=args.model,
            response_format=args.response_format,
            language=args.language,
            prompt=args.prompt,
            api_url=args.base_url,
            timeout=args.timeout,
            chunking_strategy=args.chunking_strategy,
        )
        add_recording_metadata(result, input_path)
        output_path.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    except OSError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    except ModelError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"transcription output: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
