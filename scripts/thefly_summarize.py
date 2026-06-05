#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Optional, Sequence

from models import (
    DEFAULT_MAX_OUTPUT_TOKENS as MODEL_DEFAULT_MAX_OUTPUT_TOKENS,
    DEFAULT_SUMMARY_MODEL,
    DEFAULT_TRANSCRIPTION_MODEL,
    ModelError,
    create_model,
    default_summary_output_path,
    default_transcription_output_path,
    summary_prompt,
    transcript_text,
)
import thefly_transcription


DEFAULT_MODEL = DEFAULT_SUMMARY_MODEL
DEFAULT_API_URL = "https://api.openai.com/v1/responses"
DEFAULT_MAX_OUTPUT_TOKENS = MODEL_DEFAULT_MAX_OUTPUT_TOKENS


class SummaryError(ModelError):
    pass


def default_output_path(input_path: Path) -> Path:
    return default_summary_output_path(input_path)


def read_transcript_json(input_path: Path) -> Any:
    try:
        return json.loads(input_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise SummaryError(f"input JSON is invalid: {exc}") from exc


def summarize_transcript(
    transcript: Any,
    source_name: str,
    model: str,
    api_url: str,
    timeout: float,
    max_output_tokens: int,
) -> str:
    try:
        summarizer = create_model(model, base_url=api_url, timeout=timeout)
        return summarizer._summarize_transcript(transcript, source_name, max_output_tokens)
    except ModelError as exc:
        raise SummaryError(str(exc)) from exc


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Summarize a The Fly transcription JSON or audio recording")
    parser.add_argument("input", type=Path, help="input .json transcription or audio file")
    parser.add_argument("-o", "--output", type=Path, help="output .sum.md path; defaults beside input")
    parser.add_argument("--model", default=DEFAULT_MODEL, help=f"summarization model; default: {DEFAULT_MODEL}")
    parser.add_argument("--base-url", help="OpenAI-compatible summarization base URL; defaults by model")
    parser.add_argument("--api-url", dest="base_url", help=argparse.SUPPRESS)
    parser.add_argument(
        "--transcription-model",
        default=DEFAULT_TRANSCRIPTION_MODEL,
        help=f"model to use when input is audio; default: {DEFAULT_TRANSCRIPTION_MODEL}",
    )
    parser.add_argument(
        "--transcription-response-format",
        choices=("json", "verbose_json", "diarized_json"),
        default=None,
        help="transcription response format to request when input is audio; defaults by model",
    )
    parser.add_argument("--transcription-base-url", help="OpenAI-compatible transcription base URL; defaults by model")
    parser.add_argument("--transcription-api-url", dest="transcription_base_url", help=argparse.SUPPRESS)
    parser.add_argument("--language", help="optional input language hint for audio transcription, such as en")
    parser.add_argument("--prompt", help="optional transcription prompt/context when input is audio")
    parser.add_argument("--chunking-strategy", help="optional transcription chunking strategy, such as auto")
    parser.add_argument("--timeout", type=float, default=600.0, help="API request timeout in seconds; default: 600")
    parser.add_argument(
        "--max-output-tokens",
        type=int,
        default=DEFAULT_MAX_OUTPUT_TOKENS,
        help=f"maximum summary output tokens; default: {DEFAULT_MAX_OUTPUT_TOKENS}",
    )
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    input_path: Path = args.input

    if args.timeout <= 0:
        print("error: --timeout must be positive", file=sys.stderr)
        return 2
    if args.max_output_tokens <= 0:
        print("error: --max-output-tokens must be positive", file=sys.stderr)
        return 2

    output_path: Path = args.output if args.output is not None else default_output_path(input_path)

    try:
        transcript_path = input_path
        if input_path.suffix.lower() != ".json":
            transcript_path = default_transcription_output_path(input_path)
            result = thefly_transcription.transcribe_audio_file(
                input_path=input_path,
                model=args.transcription_model,
                response_format=args.transcription_response_format,
                language=args.language,
                prompt=args.prompt,
                api_url=args.transcription_base_url,
                timeout=args.timeout,
                chunking_strategy=args.chunking_strategy,
            )
            thefly_transcription.add_recording_metadata(result, input_path)
            transcript_path.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
            print(f"transcription output: {transcript_path}")

        summary_model = create_model(args.model, base_url=args.base_url, timeout=args.timeout)
        summary_model.summarize_transcription_file(
            input_path=transcript_path,
            output_path=output_path,
            max_output_tokens=args.max_output_tokens,
        )
    except OSError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    except ModelError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"summary output: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
