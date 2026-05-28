#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import mimetypes
import os
import secrets
import struct
import sys
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any, Mapping, Optional, Sequence


#DEFAULT_MODEL = "gpt-4o-transcribe"
DEFAULT_MODEL = "gpt-4o-transcribe-diarize"
DEFAULT_API_URL = "https://api.openai.com/v1/audio/transcriptions"
MEETING_TYPE_CODES = {"C"}
MEMO_TYPE_CODES = {"M", "T", "J", "I", "R"}


class TranscriptionError(ValueError):
    pass


def default_output_path(input_path: Path) -> Path:
    return input_path.with_suffix(".trans.json")


def content_type_for_path(path: Path) -> str:
    guessed, _encoding = mimetypes.guess_type(path.name)
    return guessed or "audio/wav"


def recording_type_from_filename(path: Path) -> str:
    stem = path.stem
    type_code = stem[:1].upper()
    if len(stem) >= 2 and stem[1] == "-":
        if type_code in MEETING_TYPE_CODES:
            return "meeting"
        if type_code in MEMO_TYPE_CODES:
            return "memo"

    lower_name = path.name.lower()
    if any(word in lower_name for word in ("meeting", "call")):
        return "meeting"
    if "memo" in lower_name:
        return "memo"
    return "unknown"


def read_wav_info_icmt(wav_path: Path) -> str:
    with wav_path.open("rb") as wav:
        header = wav.read(12)
        if len(header) != 12 or header[:4] != b"RIFF" or header[8:12] != b"WAVE":
            return ""

        while True:
            chunk_header = wav.read(8)
            if len(chunk_header) < 8:
                break

            chunk_id = chunk_header[:4]
            chunk_size = struct.unpack_from("<I", chunk_header, 4)[0]
            if chunk_id == b"LIST":
                payload = wav.read(chunk_size)
                if len(payload) < chunk_size:
                    break
                if chunk_size >= 4 and payload[:4] == b"INFO":
                    comment = read_info_list_icmt(payload[4:])
                    if comment:
                        return comment
            else:
                wav.seek(chunk_size, 1)

            if chunk_size % 2:
                wav.seek(1, 1)

    return ""


def read_info_list_icmt(payload: bytes) -> str:
    offset = 0
    while offset + 8 <= len(payload):
        chunk_id = payload[offset : offset + 4]
        chunk_size = struct.unpack_from("<I", payload, offset + 4)[0]
        value_start = offset + 8
        value_end = value_start + chunk_size
        if value_end > len(payload):
            break

        if chunk_id == b"ICMT":
            return payload[value_start:value_end].rstrip(b"\x00").decode("utf-8", errors="replace").strip()

        offset = value_end + (chunk_size % 2)

    return ""


def add_recording_metadata(result: dict[str, Any], input_path: Path) -> None:
    result["meta_type"] = recording_type_from_filename(input_path)
    result["meta_comment"] = read_wav_info_icmt(input_path)


def multipart_form_data(fields: Mapping[str, str], file_field: str, file_path: Path) -> tuple[bytes, str]:
    boundary = "----thefly-" + secrets.token_hex(16)
    body = bytearray()

    def append(text: str) -> None:
        body.extend(text.encode("utf-8"))

    for name, value in fields.items():
        append(f"--{boundary}\r\n")
        append(f'Content-Disposition: form-data; name="{name}"\r\n\r\n')
        append(value)
        append("\r\n")

    append(f"--{boundary}\r\n")
    append(f'Content-Disposition: form-data; name="{file_field}"; filename="{file_path.name}"\r\n')
    append(f"Content-Type: {content_type_for_path(file_path)}\r\n\r\n")
    body.extend(file_path.read_bytes())
    append("\r\n")
    append(f"--{boundary}--\r\n")
    return bytes(body), boundary


def openai_headers(api_key: str) -> dict[str, str]:
    headers = {
        "Authorization": f"Bearer {api_key}",
        "Accept": "application/json",
    }

    organization = os.environ.get("OPENAI_ORG_ID") or os.environ.get("OPENAI_ORGANIZATION")
    if organization:
        headers["OpenAI-Organization"] = organization

    project = os.environ.get("OPENAI_PROJECT_ID") or os.environ.get("OPENAI_PROJECT")
    if project:
        headers["OpenAI-Project"] = project

    return headers


def transcribe_wav(
    input_path: Path,
    model: str,
    response_format: str,
    language: Optional[str],
    prompt: Optional[str],
    api_url: str,
    timeout: float,
    chunking_strategy: Optional[str] = None,
) -> dict[str, Any]:
    api_key = os.environ.get("OPENAI_API_KEY")
    if not api_key:
        raise TranscriptionError("OPENAI_API_KEY is not set")

    fields = {
        "model": model,
        "response_format": response_format,
    }
    if language:
        fields["language"] = language
    if prompt:
        fields["prompt"] = prompt
    if chunking_strategy:
        fields["chunking_strategy"] = chunking_strategy

    body, boundary = multipart_form_data(fields, "file", input_path)
    headers = openai_headers(api_key)
    headers["Content-Type"] = f"multipart/form-data; boundary={boundary}"

    request = urllib.request.Request(api_url, data=body, headers=headers, method="POST")
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            payload = response.read().decode("utf-8")
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        raise TranscriptionError(f"OpenAI API request failed with HTTP {exc.code}: {detail}") from exc
    except urllib.error.URLError as exc:
        raise TranscriptionError(f"OpenAI API request failed: {exc.reason}") from exc

    try:
        parsed = json.loads(payload)
    except json.JSONDecodeError as exc:
        raise TranscriptionError("OpenAI API response was not valid JSON") from exc

    if not isinstance(parsed, dict):
        raise TranscriptionError("OpenAI API response JSON was not an object")
    return parsed


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Transcribe a The Fly .wav recording with OpenAI")
    parser.add_argument("input", type=Path, help="input .wav file")
    parser.add_argument("-o", "--output", type=Path, help="output .trans.json path; defaults beside input")
    parser.add_argument("--model", default=DEFAULT_MODEL, help=f"OpenAI transcription model; default: {DEFAULT_MODEL}")
    parser.add_argument(
        "--response-format",
        choices=("json", "verbose_json", "diarized_json"),
        default="diarized_json",
        help="transcription response format to request; default: diarized_json",
    )
    parser.add_argument("--language", help="optional input language hint, such as en")
    parser.add_argument("--prompt", help="optional transcription prompt/context")
    parser.add_argument("--chunking-strategy", help="optional chunking strategy, such as auto")
    parser.add_argument("--api-url", default=DEFAULT_API_URL, help=argparse.SUPPRESS)
    parser.add_argument("--timeout", type=float, default=600.0, help="API request timeout in seconds; default: 600")
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    input_path: Path = args.input
    output_path: Path = args.output if args.output is not None else default_output_path(input_path)

    if not input_path.exists():
        print(f"error: input file does not exist: {input_path}", file=sys.stderr)
        return 2
    if not input_path.is_file():
        print(f"error: input path is not a file: {input_path}", file=sys.stderr)
        return 2
    if input_path.suffix.lower() != ".wav":
        print(f"error: input file extension is {input_path.suffix!r}; expected '.wav'", file=sys.stderr)
        return 2
    if output_path.resolve() == input_path.resolve():
        print(f"error: output path would overwrite input: {output_path}", file=sys.stderr)
        return 2
    if args.timeout <= 0:
        print("error: --timeout must be positive", file=sys.stderr)
        return 2

    try:
        result = transcribe_wav(
            input_path=input_path,
            model=args.model,
            response_format=args.response_format,
            language=args.language,
            prompt=args.prompt,
            api_url=args.api_url,
            timeout=args.timeout,
            chunking_strategy=args.chunking_strategy,
        )
        add_recording_metadata(result, input_path)
        output_path.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    except OSError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    except TranscriptionError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"transcription output: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
