#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any, Optional, Sequence


DEFAULT_MODEL = "gpt-4o-mini"
DEFAULT_API_URL = "https://api.openai.com/v1/responses"
DEFAULT_MAX_OUTPUT_TOKENS = 1800
MEMO_TYPES = {"note", "todo", "journal", "idea", "reminder"}


class SummaryError(ValueError):
    pass


def default_output_path(input_path: Path) -> Path:
    lower_name = input_path.name.lower()
    if lower_name.endswith(".trans.json"):
        return input_path.with_name(input_path.name[: -len(".trans.json")] + ".sum.md")
    return input_path.with_suffix(".sum.md")


def seconds_to_timestamp(value: Any) -> str:
    try:
        total_seconds = max(0, int(round(float(value))))
    except (TypeError, ValueError):
        return "--:--"

    hours = total_seconds // 3600
    minutes = (total_seconds % 3600) // 60
    seconds = total_seconds % 60
    if hours:
        return f"{hours:02d}:{minutes:02d}:{seconds:02d}"
    return f"{minutes:02d}:{seconds:02d}"


def segment_to_line(segment: dict[str, Any]) -> Optional[str]:
    text = str(segment.get("text") or "").strip()
    if not text:
        return None

    start = seconds_to_timestamp(segment.get("start"))
    end = seconds_to_timestamp(segment.get("end"))
    speaker = str(segment.get("speaker") or "").strip()
    prefix = f"[{start}-{end}]"
    if speaker:
        prefix += f" {speaker}:"
    return f"{prefix} {text}"


def transcript_text(transcript: Any) -> str:
    if isinstance(transcript, dict):
        segments = transcript.get("segments")
        if isinstance(segments, list):
            lines = [segment_to_line(segment) for segment in segments if isinstance(segment, dict)]
            compact_lines = [line for line in lines if line]
            if compact_lines:
                return "\n".join(compact_lines)

        text = transcript.get("text")
        if isinstance(text, str) and text.strip():
            return text.strip()

    return json.dumps(transcript, ensure_ascii=False, indent=2)


def read_transcript_json(input_path: Path) -> Any:
    try:
        return json.loads(input_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise SummaryError(f"input JSON is invalid: {exc}") from exc


def normalized_meta_type(transcript: Any) -> str:
    if not isinstance(transcript, dict):
        return "unknown"
    value = str(transcript.get("meta_type") or "").strip().lower()
    return value if value in ("meeting", "memo") else "unknown"


def normalized_meta_comment(transcript: Any) -> str:
    if not isinstance(transcript, dict):
        return ""
    return str(transcript.get("meta_comment") or "").strip()


def memo_type_from_comment(comment: str) -> str:
    lower_comment = comment.lower()
    for memo_type in MEMO_TYPES:
        if memo_type in lower_comment:
            return memo_type
    return ""


def openai_headers(api_key: str) -> dict[str, str]:
    headers = {
        "Authorization": f"Bearer {api_key}",
        "Content-Type": "application/json",
        "Accept": "application/json",
    }

    organization = os.environ.get("OPENAI_ORG_ID") or os.environ.get("OPENAI_ORGANIZATION")
    if organization:
        headers["OpenAI-Organization"] = organization

    project = os.environ.get("OPENAI_PROJECT_ID") or os.environ.get("OPENAI_PROJECT")
    if project:
        headers["OpenAI-Project"] = project

    return headers


def extract_response_text(response: dict[str, Any]) -> str:
    output_text = response.get("output_text")
    if isinstance(output_text, str) and output_text.strip():
        return output_text.strip()

    pieces: list[str] = []
    for item in response.get("output", []):
        if not isinstance(item, dict):
            continue
        for content in item.get("content", []):
            if not isinstance(content, dict):
                continue
            if content.get("type") == "output_text" and isinstance(content.get("text"), str):
                pieces.append(content["text"])

    text = "".join(pieces).strip()
    if not text:
        raise SummaryError("OpenAI API response did not contain output text")
    return text


def summary_prompt(transcript: Any, source_name: str) -> tuple[str, str]:
    meta_type = normalized_meta_type(transcript)
    meta_comment = normalized_meta_comment(transcript)
    metadata_line = f"Source file: {source_name}\nRecording type: {meta_type}\nMetadata comment: {meta_comment or '(blank)'}"

    if meta_type == "meeting":
        instructions = (
            "You summarize meeting or call transcripts. Write concise Markdown. "
            "Preserve useful timestamps when available. Focus on what was discussed, decisions, action items, owners, deadlines, risks, and open questions. "
            "Do not invent owners or deadlines; mark them as unspecified when unclear."
        )
        request = (
            f"{metadata_line}\n\n"
            "Create a meeting summary with these sections:\n"
            "# Summary\n"
            "## Key Points\n"
            "## Decisions\n"
            "## Action Items\n"
            "## Open Questions\n"
            "## Timeline\n\n"
        )
        return instructions, request

    if meta_type == "memo":
        memo_type = memo_type_from_comment(meta_comment)
        instructions = (
            "You summarize personal voice memos. Write concise Markdown. "
            "Use the metadata comment as a hint for the memo type when it matches note, todo, journal, idea, or reminder. "
            "Preserve useful timestamps when available. Do not invent details."
        )
        if memo_type == "todo":
            sections = "# Summary\n## Todo Items\n## Context\n## Follow-Up\n"
        elif memo_type == "journal":
            sections = "# Summary\n## Journal Notes\n## Themes\n## Follow-Up\n"
        elif memo_type == "idea":
            sections = "# Summary\n## Idea\n## Why It Matters\n## Next Steps\n## Open Questions\n"
        elif memo_type == "reminder":
            sections = "# Summary\n## Reminder\n## Time or Trigger\n## Follow-Up\n"
        else:
            sections = "# Summary\n## Key Points\n## Follow-Up\n## Timeline\n"

        request = (
            f"{metadata_line}\nMemo subtype hint: {memo_type or 'unspecified'}\n\n"
            "Create a memo summary with these sections:\n"
            f"{sections}\n"
        )
        return instructions, request

    instructions = (
        "You summarize audio transcripts. Write concise Markdown. Preserve useful timestamps when available. "
        "Identify any clear action items, decisions, reminders, ideas, or open questions. Do not invent details."
    )
    request = (
        f"{metadata_line}\n\n"
        "Create a summary with these sections:\n"
        "# Summary\n"
        "## Key Points\n"
        "## Action Items\n"
        "## Open Questions\n"
        "## Timeline\n\n"
    )
    return instructions, request


def summarize_transcript(
    transcript: Any,
    source_name: str,
    model: str,
    api_url: str,
    timeout: float,
    max_output_tokens: int,
) -> str:
    api_key = os.environ.get("OPENAI_API_KEY")
    if not api_key:
        raise SummaryError("OPENAI_API_KEY is not set")

    text = transcript_text(transcript)
    if not text:
        raise SummaryError("transcript is empty")

    instructions, request = summary_prompt(transcript, source_name)
    user_input = request + "Transcript:\n" + text

    payload: dict[str, Any] = {
        "model": model,
        "instructions": instructions,
        "input": user_input,
        "max_output_tokens": max_output_tokens,
    }

    data = json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(api_url, data=data, headers=openai_headers(api_key), method="POST")
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            response_body = response.read().decode("utf-8")
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        raise SummaryError(f"OpenAI API request failed with HTTP {exc.code}: {detail}") from exc
    except urllib.error.URLError as exc:
        raise SummaryError(f"OpenAI API request failed: {exc.reason}") from exc

    try:
        parsed = json.loads(response_body)
    except json.JSONDecodeError as exc:
        raise SummaryError("OpenAI API response was not valid JSON") from exc
    if not isinstance(parsed, dict):
        raise SummaryError("OpenAI API response JSON was not an object")
    return extract_response_text(parsed)


def transcribe_wav(input_path: Path, timeout: float) -> Path:
    try:
        import thefly_transcription
    except ModuleNotFoundError:
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        import thefly_transcription

    output_path = thefly_transcription.default_output_path(input_path)
    result = thefly_transcription.transcribe_wav(
        input_path=input_path,
        model=thefly_transcription.DEFAULT_MODEL,
        response_format="diarized_json",
        language=None,
        prompt=None,
        api_url=thefly_transcription.DEFAULT_API_URL,
        timeout=timeout,
        chunking_strategy="auto",
    )
    thefly_transcription.add_recording_metadata(result, input_path)
    output_path.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return output_path


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Summarize a The Fly transcription JSON or WAV recording with OpenAI")
    parser.add_argument("input", type=Path, help="input .json transcription or .wav recording")
    parser.add_argument("-o", "--output", type=Path, help="output .sum.md path; defaults beside input")
    parser.add_argument("--model", default=DEFAULT_MODEL, help=f"OpenAI summarization model; default: {DEFAULT_MODEL}")
    parser.add_argument("--api-url", default=DEFAULT_API_URL, help=argparse.SUPPRESS)
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

    if not input_path.exists():
        print(f"error: input file does not exist: {input_path}", file=sys.stderr)
        return 2
    if not input_path.is_file():
        print(f"error: input path is not a file: {input_path}", file=sys.stderr)
        return 2
    if args.timeout <= 0:
        print("error: --timeout must be positive", file=sys.stderr)
        return 2
    if args.max_output_tokens <= 0:
        print("error: --max-output-tokens must be positive", file=sys.stderr)
        return 2

    suffix = input_path.suffix.lower()
    if suffix not in (".json", ".wav"):
        print(f"error: input file extension is {input_path.suffix!r}; expected '.json' or '.wav'", file=sys.stderr)
        return 2

    output_path: Path = args.output if args.output is not None else default_output_path(input_path)
    if output_path.resolve() == input_path.resolve():
        print(f"error: output path would overwrite input: {output_path}", file=sys.stderr)
        return 2

    try:
        transcript_path = input_path
        if suffix == ".wav":
            transcript_path = transcribe_wav(input_path, args.timeout)
            print(f"transcription output: {transcript_path}")

        transcript = read_transcript_json(transcript_path)
        summary = summarize_transcript(
            transcript=transcript,
            source_name=transcript_path.name,
            model=args.model,
            api_url=args.api_url,
            timeout=args.timeout,
            max_output_tokens=args.max_output_tokens,
        )
        output_path.write_text(summary.rstrip() + "\n", encoding="utf-8")
    except OSError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    except (SummaryError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"summary output: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
