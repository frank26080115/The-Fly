#!/usr/bin/env python3
from __future__ import annotations

import json
import mimetypes
import os
import re
import secrets
import struct
import sys
import urllib.error
import urllib.request
from datetime import datetime
from pathlib import Path
from typing import Any, Mapping, Optional, Sequence


DEFAULT_OPENAI_BASE_URL = "https://api.openai.com/v1"
DEFAULT_OPENAI_TRANSCRIPTION_URL = DEFAULT_OPENAI_BASE_URL + "/audio/transcriptions"
DEFAULT_OPENAI_RESPONSES_URL = DEFAULT_OPENAI_BASE_URL + "/responses"
DEFAULT_LOCAL_OPENAI_BASE_URL = "http://127.0.0.1:11434/v1"
DEFAULT_TRANSCRIPTION_MODEL = "faster-whisper;small;cpu;int8" #"gpt-4o-transcribe-diarize"
DEFAULT_SUMMARY_MODEL = "qwen3:14b" #"gpt-4o-mini"
DEFAULT_MAX_OUTPUT_TOKENS = 1800
MEMO_TYPES = {"note", "todo", "journal", "idea", "reminder"}

FILENAME_TYPE_COMMENTS = {
    "C": "recording-type:call",
    "M": "memo-type:note",
    "T": "memo-type:todo",
    "J": "memo-type:journal",
    "I": "memo-type:idea",
    "R": "memo-type:reminder",
}
FILENAME_TYPE_PROMPTS = {
    "C": (
        "This is probably a phone or Bluetooth call recording. The audio is stereo: "
        "the left channel is usually the local device microphone and the right channel "
        "is usually the remote Bluetooth caller. Treat the channels as likely different "
        "speakers and use that as a strong hint for diarization, while still following "
        "the audio if there is overlap, silence, or channel leakage."
    ),
    "M": (
        "This is probably a spoken memo note. Transcribe it as a single-person note unless "
        "the audio clearly contains multiple speakers. Preserve wording faithfully."
    ),
    "T": (
        "This is probably a spoken todo memo. Transcribe task wording carefully, including "
        "action items, deadlines, names, quantities, and any ordering words such as first, "
        "next, or later."
    ),
    "J": (
        "This is probably a spoken journal memo. Transcribe reflectively and faithfully, "
        "preserving first-person phrasing, emotional nuance, and chronology."
    ),
    "I": (
        "This is probably a spoken idea memo. Transcribe exploratory wording faithfully, "
        "including tentative phrases, alternatives, sketches of concepts, and examples."
    ),
    "R": (
        "This is probably a spoken reminder memo. Transcribe reminder details carefully, "
        "especially dates, times, names, places, objects, and conditions."
    ),
}


class ModelError(ValueError):
    pass


class ModelNotImplementedError(ModelError, NotImplementedError):
    pass


def default_transcription_output_path(input_path: Path) -> Path:
    return input_path.with_suffix(".trans.json")


def default_summary_output_path(input_path: Path) -> Path:
    lower_name = input_path.name.lower()
    if lower_name.endswith(".trans.json"):
        return input_path.with_name(input_path.name[: -len(".trans.json")] + ".sum.md")
    return input_path.with_suffix(".sum.md")


def content_type_for_path(path: Path) -> str:
    guessed, _encoding = mimetypes.guess_type(path.name)
    return guessed or "application/octet-stream"


def metadata_comment_from_filename(path: Path) -> str:
    stem = path.stem
    type_code = stem[:1].upper()
    if len(stem) >= 2 and stem[1] == "-":
        return FILENAME_TYPE_COMMENTS.get(type_code, "")
    return ""


def default_prompt_from_filename(path: Path) -> str:
    stem = path.stem
    type_code = stem[:1].upper()
    if len(stem) >= 2 and stem[1] == "-":
        return FILENAME_TYPE_PROMPTS.get(type_code, "")
    return ""


def metadata_values(comment: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in comment.splitlines():
        key, separator, value = line.partition(":")
        if separator:
            values[key.strip().lower()] = value.strip().lower()
    return values


def recording_type_from_metadata(comment: str) -> str:
    values = metadata_values(comment)

    recording_type = values.get("recording-type", "")
    if recording_type in ("call", "meeting"):
        return "meeting"
    if recording_type == "memo":
        return "memo"

    if values.get("memo-type") in MEMO_TYPES:
        return "memo"

    if any(key in values for key in ("call-state", "call", "callsetup", "callheld", "caller-info", "sco-audio")):
        return "meeting"

    return "unknown"


def read_wav_info_icmt(wav_path: Path) -> str:
    try:
        wav = wav_path.open("rb")
    except OSError:
        return ""

    with wav:
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
    meta_comment = read_wav_info_icmt(input_path)
    if not meta_comment:
        meta_comment = metadata_comment_from_filename(input_path)

    result["meta_type"] = recording_type_from_metadata(meta_comment)
    result["meta_comment"] = meta_comment


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


def default_local_base_url() -> str:
    for name in ("THEFLY_LOCAL_OPENAI_BASE_URL", "OLLAMA_OPENAI_BASE_URL"):
        value = os.environ.get(name)
        if value:
            return normalize_base_url(value)

    for name in ("OLLAMA_BASE_URL", "OLLAMA_HOST"):
        value = os.environ.get(name)
        if value:
            if "://" not in value:
                value = "http://" + value
            return normalize_base_url(value)

    return DEFAULT_LOCAL_OPENAI_BASE_URL


def normalize_base_url(value: str) -> str:
    base_url = value.rstrip("/")
    if re.search(r"/v\d+$", base_url):
        return base_url
    if base_url.endswith(("/responses", "/chat/completions", "/audio/transcriptions")):
        return base_url
    if "11434" in base_url:
        return base_url + "/v1"
    return base_url


def endpoint_url(base_url: str, endpoint: str) -> str:
    normalized = normalize_base_url(base_url)
    if normalized.endswith("/" + endpoint) or normalized.endswith(endpoint):
        return normalized
    return f"{normalized}/{endpoint}"


def normalize_model_key(name: str) -> str:
    return re.sub(r"[\s_\-:.]+", "", name.strip().lower())


def split_model_spec(model_name: str) -> tuple[str, list[str]]:
    parts = [part.strip() for part in model_name.split(";")]
    base_name = parts[0]
    params = parts[1:]
    if not base_name:
        raise ModelError("model name is blank")
    if any(param == "" for param in params):
        raise ModelError(f"model parameters cannot be blank in {model_name!r}")
    return base_name, params


def print_progress(message: str) -> None:
    print(f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] {message}", file=sys.stderr, flush=True)


def format_byte_count(byte_count: int) -> str:
    value = float(max(0, byte_count))
    for suffix in ("B", "KB", "MB", "GB"):
        if value < 1024.0 or suffix == "GB":
            if suffix == "B":
                return f"{int(value)} {suffix}"
            return f"{value:.1f} {suffix}"
        value /= 1024.0
    return f"{value:.1f} GB"


def validate_local_transcription_response_format(response_format: Optional[str], label: str) -> None:
    if response_format not in (None, "json", "verbose_json"):
        raise ModelError(f"{label} supports response formats json and verbose_json, not {response_format!r}")


def json_safe(value: Any) -> Any:
    if value is None or isinstance(value, (str, int, float, bool)):
        return value
    if isinstance(value, Mapping):
        return {str(key): json_safe(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [json_safe(item) for item in value]
    item = getattr(value, "item", None)
    if callable(item):
        try:
            return json_safe(item())
        except (TypeError, ValueError):
            pass
    return str(value)


def maybe_text_only_transcription(result: dict[str, Any], response_format: Optional[str]) -> dict[str, Any]:
    if response_format != "json":
        return result

    text = result.get("text")
    text_only: dict[str, Any] = {"text": text if isinstance(text, str) else ""}
    for key in ("language", "duration"):
        if key in result:
            text_only[key] = result[key]
    return text_only


def faster_whisper_segment_to_dict(segment: Any) -> dict[str, Any]:
    item: dict[str, Any] = {}
    for key in (
        "id",
        "seek",
        "start",
        "end",
        "text",
        "tokens",
        "temperature",
        "avg_logprob",
        "compression_ratio",
        "no_speech_prob",
    ):
        if hasattr(segment, key):
            item[key] = json_safe(getattr(segment, key))

    words = getattr(segment, "words", None)
    if words:
        item["words"] = [
            {
                key: json_safe(getattr(word, key))
                for key in ("start", "end", "word", "probability")
                if hasattr(word, key)
            }
            for word in words
        ]

    return item


def faster_whisper_info_to_dict(info: Any) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key in ("language", "language_probability", "duration", "duration_after_vad", "all_language_probs"):
        if hasattr(info, key):
            result[key] = json_safe(getattr(info, key))
    return result


class AIModel:
    model_name = ""
    display_name = ""
    api_model = ""
    default_base_url = ""
    requires_api_key = True
    env_api_key = "OPENAI_API_KEY"

    def __init__(self, base_url: Optional[str] = None, api_key: Optional[str] = None, timeout: float = 600.0) -> None:
        self.base_url = normalize_base_url(base_url or self.default_base_url)
        self.api_key = api_key
        self.timeout = timeout

    def configure_model_spec(self, params: Sequence[str], original_model_name: str) -> None:
        if params:
            raise ModelError(f"{self.display_name or self.model_name} does not accept semicolon model parameters")

    def transcribe_audio_file(
        self,
        input_path: Path,
        output_path: Path,
        response_format: Optional[str] = None,
        language: Optional[str] = None,
        prompt: Optional[str] = None,
        chunking_strategy: Optional[str] = None,
    ) -> Path:
        input_path = Path(input_path)
        output_path = Path(output_path)
        validate_input_file(input_path)
        validate_output_path(input_path, output_path)

        result = self._transcribe_audio(input_path, response_format, language, prompt, chunking_strategy)
        add_recording_metadata(result, input_path)
        output_path.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        return output_path

    def summarize_transcription_file(
        self,
        input_path: Path,
        output_path: Path,
        max_output_tokens: int = DEFAULT_MAX_OUTPUT_TOKENS,
    ) -> Path:
        input_path = Path(input_path)
        output_path = Path(output_path)
        validate_input_file(input_path)
        validate_output_path(input_path, output_path)
        if max_output_tokens <= 0:
            raise ModelError("max_output_tokens must be positive")

        try:
            transcript = json.loads(input_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            raise ModelError(f"input JSON is invalid: {exc}") from exc

        summary = self._summarize_transcript(transcript, input_path.name, max_output_tokens)
        output_path.write_text(summary.rstrip() + "\n", encoding="utf-8")
        return output_path

    def _transcribe_audio(
        self,
        input_path: Path,
        response_format: Optional[str],
        language: Optional[str],
        prompt: Optional[str],
        chunking_strategy: Optional[str],
    ) -> dict[str, Any]:
        raise ModelNotImplementedError(f"{self.display_name or self.model_name} does not implement transcription")

    def _summarize_transcript(self, transcript: Any, source_name: str, max_output_tokens: int) -> str:
        raise ModelNotImplementedError(f"{self.display_name or self.model_name} does not implement summarization")

    def resolved_api_key(self) -> Optional[str]:
        if self.api_key:
            return self.api_key
        return os.environ.get(self.env_api_key)

    def headers(self, json_body: bool = False) -> dict[str, str]:
        api_key = self.resolved_api_key()
        if self.requires_api_key and not api_key:
            raise ModelError(f"{self.env_api_key} is not set")

        headers = {"Accept": "application/json"}
        if json_body:
            headers["Content-Type"] = "application/json"
        if api_key:
            headers["Authorization"] = f"Bearer {api_key}"

        if self.requires_api_key:
            organization = os.environ.get("OPENAI_ORG_ID") or os.environ.get("OPENAI_ORGANIZATION")
            if organization:
                headers["OpenAI-Organization"] = organization

            project = os.environ.get("OPENAI_PROJECT_ID") or os.environ.get("OPENAI_PROJECT")
            if project:
                headers["OpenAI-Project"] = project

        return headers

    def post_json(self, endpoint: str, payload: dict[str, Any]) -> dict[str, Any]:
        data = json.dumps(payload).encode("utf-8")
        request = urllib.request.Request(
            endpoint_url(self.base_url, endpoint),
            data=data,
            headers=self.headers(json_body=True),
            method="POST",
        )
        return read_json_response(request, self.timeout, self.display_name or self.model_name)

    def print_uploading_file(self, input_path: Path, purpose: str) -> None:
        size_text = ""
        try:
            size_text = f", {format_byte_count(input_path.stat().st_size)}"
        except OSError:
            pass
        print_progress(f"{self.model_label()}: uploading {input_path}{size_text} for {purpose}")

    def print_processing_file(self, input_path: Path, purpose: str) -> None:
        size_text = ""
        try:
            size_text = f", {format_byte_count(input_path.stat().st_size)}"
        except OSError:
            pass
        print_progress(f"{self.model_label()}: processing {input_path}{size_text} for {purpose}")

    def print_submitting(self, source_name: str, purpose: str) -> None:
        print_progress(f"{self.model_label()}: submitting {source_name} for {purpose}")

    def print_returned(self, source_name: str, purpose: str) -> None:
        print_progress(f"{self.model_label()}: returned {purpose} result for {source_name}")

    def model_label(self) -> str:
        label = self.display_name or self.model_name or self.__class__.__name__
        if self.api_model and self.api_model != label:
            return f"{label} [{self.api_model}]"
        return label


class OpenAICompatibleTranscriptionModel(AIModel):
    default_base_url = DEFAULT_OPENAI_TRANSCRIPTION_URL
    default_response_format = "json"
    default_chunking_strategy: Optional[str] = None
    supports_prompt = True

    def _transcribe_audio(
        self,
        input_path: Path,
        response_format: Optional[str],
        language: Optional[str],
        prompt: Optional[str],
        chunking_strategy: Optional[str],
    ) -> dict[str, Any]:
        fields = {
            "model": self.api_model,
            "response_format": response_format or self.default_response_format,
        }
        if language:
            fields["language"] = language
        if self.supports_prompt and not prompt:
            prompt = default_prompt_from_filename(input_path)
        if self.supports_prompt and prompt:
            fields["prompt"] = prompt

        effective_chunking = chunking_strategy if chunking_strategy is not None else self.default_chunking_strategy
        if effective_chunking:
            fields["chunking_strategy"] = effective_chunking

        self.print_uploading_file(input_path, "transcription")
        body, boundary = multipart_form_data(fields, "file", input_path)
        headers = self.headers()
        headers["Content-Type"] = f"multipart/form-data; boundary={boundary}"
        request = urllib.request.Request(
            endpoint_url(self.base_url, "audio/transcriptions"),
            data=body,
            headers=headers,
            method="POST",
        )
        response_text = read_text_response(request, self.timeout, self.display_name or self.model_name)
        self.print_returned(str(input_path), "transcription")
        try:
            parsed = json.loads(response_text)
        except json.JSONDecodeError:
            parsed = {"text": response_text.strip()}
        if not isinstance(parsed, dict):
            raise ModelError("transcription response JSON was not an object")
        return parsed


class GPT4oTranscribeModel(OpenAICompatibleTranscriptionModel):
    model_name = "gpt-4o-transcribe"
    display_name = "GPT-4o Transcribe"
    api_model = "gpt-4o-transcribe"
    default_response_format = "verbose_json"


class GPT4oTranscribeDiarizeModel(OpenAICompatibleTranscriptionModel):
    model_name = "gpt-4o-transcribe-diarize"
    display_name = "GPT-4o Transcribe Diarize"
    api_model = "gpt-4o-transcribe-diarize"
    default_response_format = "diarized_json"
    default_chunking_strategy = "auto"
    supports_prompt = False


class OpenAIWhisperModel(OpenAICompatibleTranscriptionModel):
    model_name = "whisper-1"
    display_name = "OpenAI Whisper API"
    api_model = "whisper-1"
    default_response_format = "verbose_json"


class WhisperModel(AIModel):
    model_name = "openai-whisper"
    display_name = "OpenAI Whisper"
    requires_api_key = False
    default_model_size = os.environ.get("THEFLY_OPENAI_WHISPER_MODEL", "small")

    def __init__(self, base_url: Optional[str] = None, api_key: Optional[str] = None, timeout: float = 600.0) -> None:
        super().__init__(base_url=base_url, api_key=api_key, timeout=timeout)
        self.model_size = self.default_model_size
        self.api_model = self.model_size
        self._loaded_model: Any = None

    def configure_model_spec(self, params: Sequence[str], original_model_name: str) -> None:
        if len(params) > 1:
            raise ModelError("openai-whisper model spec is openai-whisper;<model-size>")
        if params:
            self.model_size = params[0]
        self.api_model = self.model_size

    def _transcribe_audio(
        self,
        input_path: Path,
        response_format: Optional[str],
        language: Optional[str],
        prompt: Optional[str],
        chunking_strategy: Optional[str],
    ) -> dict[str, Any]:
        validate_local_transcription_response_format(response_format, self.display_name)
        if chunking_strategy:
            raise ModelError("OpenAI Whisper local transcription does not support chunking_strategy")

        try:
            import whisper as openai_whisper
        except ImportError as exc:
            raise ModelError("openai-whisper is not installed; run: pip install openai-whisper") from exc

        if self._loaded_model is None:
            print_progress(f"{self.model_label()}: loading local model")
            self._loaded_model = openai_whisper.load_model(self.model_size)

        effective_prompt = prompt or default_prompt_from_filename(input_path)
        options: dict[str, Any] = {"verbose": False}
        if language:
            options["language"] = language
        if effective_prompt:
            options["initial_prompt"] = effective_prompt

        self.print_processing_file(input_path, "transcription")
        result = json_safe(self._loaded_model.transcribe(str(input_path), **options))
        self.print_returned(str(input_path), "transcription")
        if not isinstance(result, dict):
            raise ModelError("OpenAI Whisper local transcription did not return a JSON object")
        result["model"] = self.model_name
        result["local_model"] = self.model_size
        return maybe_text_only_transcription(result, response_format)


class FasterWhisperModel(AIModel):
    model_name = "faster-whisper"
    display_name = "Faster-Whisper"
    requires_api_key = False
    default_model_size = os.environ.get("THEFLY_FASTER_WHISPER_MODEL", "small")
    default_device = os.environ.get("THEFLY_FASTER_WHISPER_DEVICE", "cpu")
    default_compute_type = os.environ.get("THEFLY_FASTER_WHISPER_COMPUTE_TYPE", "int8")

    def __init__(self, base_url: Optional[str] = None, api_key: Optional[str] = None, timeout: float = 600.0) -> None:
        super().__init__(base_url=base_url, api_key=api_key, timeout=timeout)
        self.model_size = self.default_model_size
        self.device = self.default_device
        self.compute_type = self.default_compute_type
        self.api_model = self.model_spec_label()
        self._loaded_model: Any = None

    def configure_model_spec(self, params: Sequence[str], original_model_name: str) -> None:
        if len(params) > 3:
            raise ModelError("faster-whisper model spec is faster-whisper;<model-size>;<device>;<compute-type>")
        if len(params) >= 1:
            self.model_size = params[0]
        if len(params) >= 2:
            self.device = params[1]
        if len(params) >= 3:
            self.compute_type = params[2]
        self.api_model = self.model_spec_label()

    def model_spec_label(self) -> str:
        return f"{self.model_size};{self.device};{self.compute_type}"

    def _transcribe_audio(
        self,
        input_path: Path,
        response_format: Optional[str],
        language: Optional[str],
        prompt: Optional[str],
        chunking_strategy: Optional[str],
    ) -> dict[str, Any]:
        validate_local_transcription_response_format(response_format, self.display_name)
        if chunking_strategy:
            raise ModelError("Faster-Whisper local transcription does not support chunking_strategy")

        try:
            from faster_whisper import WhisperModel as FasterWhisperEngine
        except ImportError as exc:
            raise ModelError("faster-whisper is not installed; run: pip install faster-whisper") from exc

        if self._loaded_model is None:
            print_progress(f"{self.model_label()}: loading local model")
            self._loaded_model = FasterWhisperEngine(
                self.model_size,
                device=self.device,
                compute_type=self.compute_type,
            )

        effective_prompt = prompt or default_prompt_from_filename(input_path)
        options: dict[str, Any] = {}
        if language:
            options["language"] = language
        if effective_prompt:
            options["initial_prompt"] = effective_prompt

        self.print_processing_file(input_path, "transcription")
        segments_iter, info = self._loaded_model.transcribe(str(input_path), **options)
        segments = [faster_whisper_segment_to_dict(segment) for segment in segments_iter]
        text = " ".join(str(segment.get("text") or "").strip() for segment in segments).strip()
        result = {
            "text": text,
            "segments": segments,
            "model": self.model_name,
            "local_model": self.model_size,
            "device": self.device,
            "compute_type": self.compute_type,
        }
        result.update(faster_whisper_info_to_dict(info))
        self.print_returned(str(input_path), "transcription")
        return maybe_text_only_transcription(result, response_format)


class ParakeetModel(OpenAICompatibleTranscriptionModel):
    model_name = "parakeet"
    display_name = "Parakeet"
    api_model = os.environ.get("THEFLY_PARAKEET_MODEL", "parakeet")
    default_base_url = default_local_base_url()
    requires_api_key = False
    env_api_key = "THEFLY_LOCAL_OPENAI_API_KEY"
    default_response_format = "verbose_json"


class OpenAIResponsesSummaryModel(AIModel):
    default_base_url = DEFAULT_OPENAI_RESPONSES_URL

    def _summarize_transcript(self, transcript: Any, source_name: str, max_output_tokens: int) -> str:
        text = transcript_text(transcript)
        if not text:
            raise ModelError("transcript is empty")

        instructions, request = summary_prompt(transcript, source_name)
        payload = {
            "model": self.api_model,
            "instructions": instructions,
            "input": request + "Transcript:\n" + text,
            "max_output_tokens": max_output_tokens,
        }
        self.print_submitting(source_name, "summarization")
        response = self.post_json("responses", payload)
        self.print_returned(source_name, "summarization")
        return extract_responses_text(response)


class GPT4oMiniModel(OpenAIResponsesSummaryModel):
    model_name = "gpt-4o-mini"
    display_name = "GPT-4o mini"
    api_model = "gpt-4o-mini"


class OpenAICompatibleChatSummaryModel(AIModel):
    default_base_url = default_local_base_url()
    requires_api_key = False
    env_api_key = "THEFLY_LOCAL_OPENAI_API_KEY"

    def _summarize_transcript(self, transcript: Any, source_name: str, max_output_tokens: int) -> str:
        text = transcript_text(transcript)
        if not text:
            raise ModelError("transcript is empty")

        instructions, request = summary_prompt(transcript, source_name)
        payload = {
            "model": self.api_model,
            "messages": [
                {"role": "system", "content": instructions},
                {"role": "user", "content": request + "Transcript:\n" + text},
            ],
            "max_tokens": max_output_tokens,
            "stream": False,
        }
        self.print_submitting(source_name, "summarization")
        response = self.post_json("chat/completions", payload)
        self.print_returned(source_name, "summarization")
        return extract_chat_text(response)


class Qwen3Model(OpenAICompatibleChatSummaryModel):
    model_name = "qwen3"
    display_name = "Qwen 3"
    api_model = os.environ.get("THEFLY_QWEN3_MODEL", "qwen3")


class Gemma3Model(OpenAICompatibleChatSummaryModel):
    model_name = "gemma3"
    display_name = "Gemma 3"
    api_model = os.environ.get("THEFLY_GEMMA3_MODEL", "gemma3")


class Llama33Model(OpenAICompatibleChatSummaryModel):
    model_name = "llama3.3"
    display_name = "Llama 3.3"
    api_model = os.environ.get("THEFLY_LLAMA33_MODEL", "llama3.3")


class DeepSeekR1Model(OpenAICompatibleChatSummaryModel):
    model_name = "deepseek-r1"
    display_name = "DeepSeek-R1"
    api_model = os.environ.get("THEFLY_DEEPSEEK_R1_MODEL", "deepseek-r1")


MODEL_CLASSES: dict[str, type[AIModel]] = {
    "gpt4otranscribe": GPT4oTranscribeModel,
    "gpt4otranscribediarize": GPT4oTranscribeDiarizeModel,
    "whisper": WhisperModel,
    "openaiwhisper": WhisperModel,
    "openaiwhisperapi": OpenAIWhisperModel,
    "whisper1": OpenAIWhisperModel,
    "fasterwhisper": FasterWhisperModel,
    "parakeet": ParakeetModel,
    "gpt4omini": GPT4oMiniModel,
    "qwen3": Qwen3Model,
    "qwen": Qwen3Model,
    "gemma3": Gemma3Model,
    "gemma": Gemma3Model,
    "llama33": Llama33Model,
    "llama3": Llama33Model,
    "deepseekr1": DeepSeekR1Model,
    "deepseek": DeepSeekR1Model,
}

MODEL_PREFIX_CLASSES: tuple[tuple[str, type[AIModel]], ...] = (
    ("gpt4otranscribediarize", GPT4oTranscribeDiarizeModel),
    ("gpt4otranscribe", GPT4oTranscribeModel),
    ("gpt4omini", GPT4oMiniModel),
    ("openaiwhisperapi", OpenAIWhisperModel),
    ("openaiwhisper", WhisperModel),
    ("whisper1", OpenAIWhisperModel),
    ("fasterwhisper", FasterWhisperModel),
    ("parakeet", ParakeetModel),
    ("whisper", WhisperModel),
    ("deepseekr1", DeepSeekR1Model),
    ("deepseek", DeepSeekR1Model),
    ("llama33", Llama33Model),
    ("llama3", Llama33Model),
    ("gemma3", Gemma3Model),
    ("gemma", Gemma3Model),
    ("qwen3", Qwen3Model),
    ("qwen", Qwen3Model),
)


def create_model(
    model_name: str,
    base_url: Optional[str] = None,
    api_key: Optional[str] = None,
    timeout: float = 600.0,
) -> AIModel:
    model_base_name, params = split_model_spec(model_name)
    normalized_key = normalize_model_key(model_base_name)
    model_class = MODEL_CLASSES.get(normalized_key)
    requested_api_model = ""
    if not model_class:
        for prefix, prefix_model_class in MODEL_PREFIX_CLASSES:
            if normalized_key.startswith(prefix):
                model_class = prefix_model_class
                requested_api_model = model_base_name.strip()
                break

    if not model_class:
        raise ModelError(f"unknown model {model_name!r}; available models: {', '.join(available_model_names())}")

    model = model_class(base_url=base_url, api_key=api_key, timeout=timeout)
    model.configure_model_spec(params, model_name.strip())
    if requested_api_model:
        model.api_model = requested_api_model
    return model


def available_model_names() -> list[str]:
    names = sorted({cls.model_name for cls in MODEL_CLASSES.values()})
    return [name for name in names if name]


def validate_input_file(path: Path) -> None:
    if not path.exists():
        raise ModelError(f"input file does not exist: {path}")
    if not path.is_file():
        raise ModelError(f"input path is not a file: {path}")


def validate_output_path(input_path: Path, output_path: Path) -> None:
    if output_path.resolve() == input_path.resolve():
        raise ModelError(f"output path would overwrite input: {output_path}")


def read_text_response(request: urllib.request.Request, timeout: float, label: str) -> str:
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return response.read().decode("utf-8")
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        raise ModelError(f"{label} API request failed with HTTP {exc.code}: {detail}") from exc
    except urllib.error.URLError as exc:
        raise ModelError(f"{label} API request failed: {exc.reason}") from exc


def read_json_response(request: urllib.request.Request, timeout: float, label: str) -> dict[str, Any]:
    response_text = read_text_response(request, timeout, label)
    try:
        parsed = json.loads(response_text)
    except json.JSONDecodeError as exc:
        raise ModelError(f"{label} API response was not valid JSON") from exc
    if not isinstance(parsed, dict):
        raise ModelError(f"{label} API response JSON was not an object")
    return parsed


def extract_responses_text(response: dict[str, Any]) -> str:
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
        raise ModelError("Responses API response did not contain output text")
    return text


def extract_chat_text(response: dict[str, Any]) -> str:
    choices = response.get("choices")
    if not isinstance(choices, list) or not choices:
        raise ModelError("chat completions response did not contain choices")

    choice = choices[0]
    if not isinstance(choice, dict):
        raise ModelError("chat completions response choice was not an object")

    message = choice.get("message")
    if isinstance(message, dict):
        content = message.get("content")
        if isinstance(content, str) and content.strip():
            return content.strip()
        if isinstance(content, list):
            pieces: list[str] = []
            for part in content:
                if isinstance(part, dict) and isinstance(part.get("text"), str):
                    pieces.append(part["text"])
            text = "".join(pieces).strip()
            if text:
                return text

    text = choice.get("text")
    if isinstance(text, str) and text.strip():
        return text.strip()

    raise ModelError("chat completions response did not contain message content")


__all__ = [
    "AIModel",
    "DEFAULT_MAX_OUTPUT_TOKENS",
    "DEFAULT_SUMMARY_MODEL",
    "DEFAULT_TRANSCRIPTION_MODEL",
    "DeepSeekR1Model",
    "FasterWhisperModel",
    "GPT4oMiniModel",
    "GPT4oTranscribeDiarizeModel",
    "GPT4oTranscribeModel",
    "Gemma3Model",
    "Llama33Model",
    "ModelError",
    "ModelNotImplementedError",
    "OpenAIWhisperModel",
    "ParakeetModel",
    "Qwen3Model",
    "WhisperModel",
    "add_recording_metadata",
    "available_model_names",
    "create_model",
    "default_prompt_from_filename",
    "default_summary_output_path",
    "default_transcription_output_path",
    "summary_prompt",
    "transcript_text",
]
