#!/usr/bin/env python3
from __future__ import annotations

import argparse
import ipaddress
import json
import shutil
import socket
import struct
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Optional, Sequence

import thefly_audio_decryptor
import thefly_summarize
import thefly_transcription


DEFAULT_DB_DIR = Path.home() / "the-fly"
DEFAULT_DEVICE_TIMEOUT_SECONDS = 15.0
DEFAULT_MDNS_TIMEOUT_SECONDS = 4.0
DEFAULT_GAP_THRESHOLD_MS = 200.0
DEFAULT_API_TIMEOUT_SECONDS = 600.0
MDNS_GROUP = "224.0.0.251"
MDNS_PORT = 5353
THE_FLY_SERVICE = "_the-fly._tcp.local"

DIR_AUDIO = "audio"
DIR_TRANSCRIBED = "transcribed"
DIR_SUMMARIZED = "summarized"

DNS_TYPE_A = 1
DNS_TYPE_PTR = 12
DNS_TYPE_AAAA = 28
DNS_TYPE_SRV = 33
DNS_CLASS_IN = 1
DNS_CLASS_QU = 0x8000


class DesktopError(ValueError):
    pass


@dataclass
class DbPaths:
    root: Path
    audio: Path
    transcribed: Path
    summarized: Path


@dataclass
class DnsRecord:
    name: str
    record_type: int
    record_class: int
    ttl: int
    rdata_offset: int
    rdata: bytes


@dataclass
class MdnsService:
    instance: str
    target: str
    port: int
    address: str


def warn(message: str) -> None:
    print(f"warning: {message}", file=sys.stderr)


def default_db_paths(db_dir: Path) -> DbPaths:
    root = db_dir.expanduser()
    return DbPaths(
        root=root,
        audio=root / DIR_AUDIO,
        transcribed=root / DIR_TRANSCRIBED,
        summarized=root / DIR_SUMMARIZED,
    )


def ensure_db_dirs(paths: DbPaths) -> None:
    for directory in (paths.root, paths.audio, paths.transcribed, paths.summarized):
        directory.mkdir(parents=True, exist_ok=True)


def normalize_device_base(value: str) -> str:
    text = value.strip()
    if not text:
        raise DesktopError("device IP or URL is blank")

    if "://" not in text:
        text = "http://" + text

    parsed = urllib.parse.urlparse(text)
    if parsed.scheme not in ("http", "https") or not parsed.netloc:
        raise DesktopError(f"invalid device URL: {value}")

    return urllib.parse.urlunparse((parsed.scheme, parsed.netloc, parsed.path.rstrip("/"), "", "", ""))


def endpoint_url(base_url: str, path: str, params: Optional[dict[str, str]] = None) -> str:
    url = urllib.parse.urljoin(base_url.rstrip("/") + "/", path.lstrip("/"))
    if params:
        url += "?" + urllib.parse.urlencode(params)
    return url


def urlopen_bytes(url: str, timeout: float) -> bytes:
    try:
        with urllib.request.urlopen(url, timeout=timeout) as response:
            return response.read()
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace").strip()
        raise DesktopError(f"HTTP {exc.code} from {url}: {detail or exc.reason}") from exc
    except urllib.error.URLError as exc:
        raise DesktopError(f"could not reach {url}: {exc.reason}") from exc


def list_remote_files(base_url: str, timeout: float) -> list[str]:
    payload = urlopen_bytes(endpoint_url(base_url, "/list_files.json"), timeout)
    try:
        parsed = json.loads(payload.decode("utf-8"))
    except json.JSONDecodeError as exc:
        raise DesktopError("device /list_files.json response was not valid JSON") from exc

    if not isinstance(parsed, list) or not all(isinstance(item, str) for item in parsed):
        raise DesktopError("device /list_files.json response was not a JSON string array")

    return parsed


def safe_local_name(remote_name: str) -> str:
    normalized = remote_name.replace("\\", "/").lstrip("/")
    if not normalized or "/" in normalized or normalized in (".", ".."):
        raise DesktopError(f"remote file name is not a safe root file: {remote_name!r}")
    return normalized


def is_audio_file(file_name: str) -> bool:
    return Path(file_name).suffix.lower() in thefly_audio_decryptor.AUDIO_SUFFIXES


def is_transcribable_audio_file(file_name: str) -> bool:
    return Path(file_name).suffix.lower() in thefly_audio_decryptor.PLAIN_AUDIO_SUFFIXES


def output_audio_path_for(input_path: Path, audio_dir: Path) -> Path:
    return audio_dir / f"{input_path.stem}{thefly_audio_decryptor.output_suffix_for_input(input_path)}"


def download_remote_file(base_url: str, remote_name: str, output_path: Path, timeout: float) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    url = endpoint_url(base_url, "/download_file", {"file_name": remote_name})
    temp_path = output_path.with_name(output_path.name + ".download")

    try:
        with urllib.request.urlopen(url, timeout=timeout) as response, temp_path.open("wb") as output:
            shutil.copyfileobj(response, output)
        temp_path.replace(output_path)
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace").strip()
        raise DesktopError(f"HTTP {exc.code} downloading {remote_name}: {detail or exc.reason}") from exc
    except urllib.error.URLError as exc:
        raise DesktopError(f"could not download {remote_name}: {exc.reason}") from exc
    finally:
        try:
            temp_path.unlink()
        except FileNotFoundError:
            pass


def save_audio_directly(source_path: Path, audio_path: Path) -> Path:
    audio_path.parent.mkdir(parents=True, exist_ok=True)
    if source_path.resolve() == audio_path.resolve():
        return audio_path

    temp_path = audio_path.with_name(audio_path.name + ".download")
    try:
        shutil.copy2(source_path, temp_path)
        temp_path.replace(audio_path)
    finally:
        try:
            temp_path.unlink()
        except FileNotFoundError:
            pass
    return audio_path


def delete_remote_file(base_url: str, remote_name: str, timeout: float) -> None:
    urlopen_bytes(endpoint_url(base_url, "/delete_file", {"file_name": remote_name}), timeout)


def load_filecrypt_key(key_file: Optional[Path], password: Optional[str]) -> Optional[bytes]:
    if key_file is not None and password is not None:
        raise DesktopError("specify either --key or --password, not both")
    if key_file is not None:
        sectools = thefly_audio_decryptor.load_sectools()
        return sectools.read_key_file(key_file.expanduser())
    if password is not None:
        sectools = thefly_audio_decryptor.load_sectools()
        return sectools.derive_filecrypt_key(password)
    return None


def decode_audio_file(input_path: Path, output_path: Path, key: Optional[bytes], gap_threshold_ms: float) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)

    pcm_path = output_path.with_suffix(".pcm")
    if thefly_audio_decryptor.output_uses_pcm(input_path):
        pcm_temp = tempfile.NamedTemporaryFile(prefix="thefly-", suffix=".pcm", dir=str(output_path.parent), delete=False)
        pcm_path = Path(pcm_temp.name)
        pcm_temp.close()

    try:
        thefly_audio_decryptor.decode_recording(input_path, pcm_path, output_path, gap_threshold_ms, key)
    finally:
        if thefly_audio_decryptor.output_uses_pcm(input_path):
            try:
                pcm_path.unlink()
            except FileNotFoundError:
                pass


def transcript_path_for_audio(audio_path: Path, transcribed_dir: Path) -> Path:
    return transcribed_dir / f"{audio_path.stem}.trans.json"


def summary_path_for_stem(stem: str, summarized_dir: Path) -> Path:
    return summarized_dir / f"{stem}.sum.md"


def summary_path_for_transcript(transcript_path: Path, summarized_dir: Path) -> Path:
    name = transcript_path.name
    if name.lower().endswith(".trans.json"):
        stem = name[: -len(".trans.json")]
    else:
        stem = transcript_path.stem
    return summary_path_for_stem(stem, summarized_dir)


def transcribe_audio_to_json(audio_path: Path, output_path: Path, timeout: float) -> Path:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    result = thefly_transcription.transcribe_wav(
        input_path=audio_path,
        model=thefly_transcription.DEFAULT_MODEL,
        response_format="diarized_json",
        language=None,
        prompt=None,
        api_url=thefly_transcription.DEFAULT_API_URL,
        timeout=timeout,
        chunking_strategy="auto",
    )
    thefly_transcription.add_recording_metadata(result, audio_path)
    output_path.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"transcription output: {output_path}")
    return output_path


def summarize_transcript_to_markdown(transcript_path: Path, output_path: Path, timeout: float, max_output_tokens: int) -> Path:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    transcript = thefly_summarize.read_transcript_json(transcript_path)
    summary = thefly_summarize.summarize_transcript(
        transcript=transcript,
        source_name=transcript_path.name,
        model=thefly_summarize.DEFAULT_MODEL,
        api_url=thefly_summarize.DEFAULT_API_URL,
        timeout=timeout,
        max_output_tokens=max_output_tokens,
    )
    output_path.write_text(summary.rstrip() + "\n", encoding="utf-8")
    print(f"summary output: {output_path}")
    return output_path


def transcribe_and_summarize_audio(
    audio_path: Path,
    paths: DbPaths,
    timeout: float,
    max_output_tokens: int,
    force: bool = False,
) -> None:
    transcript_path = transcript_path_for_audio(audio_path, paths.transcribed)
    if force or not transcript_path.exists():
        transcribe_audio_to_json(audio_path, transcript_path, timeout)
    else:
        print(f"transcription exists: {transcript_path}")

    summary_path = summary_path_for_stem(audio_path.stem, paths.summarized)
    if force or not summary_path.exists():
        summarize_transcript_to_markdown(transcript_path, summary_path, timeout, max_output_tokens)
    else:
        print(f"summary exists: {summary_path}")


def handle_transcribe_file(input_path: Path, paths: DbPaths, key: Optional[bytes], args: argparse.Namespace) -> None:
    source = input_path.expanduser()
    if not source.exists() or not source.is_file():
        raise DesktopError(f"--transcribe-file is not a file: {source}")

    suffix = source.suffix.lower()
    if suffix in thefly_audio_decryptor.ENCRYPTED_AUDIO_SUFFIXES:
        audio_path = output_audio_path_for(source, paths.audio)
        decode_audio_file(source, audio_path, key, args.gap_threshold_ms)
        transcribe_and_summarize_audio(audio_path, paths, args.api_timeout, args.max_output_tokens, force=args.force_transcribe)
    elif suffix in thefly_audio_decryptor.PLAIN_AUDIO_SUFFIXES:
        audio_path = save_audio_directly(source, paths.audio / source.name)
        transcribe_and_summarize_audio(audio_path, paths, args.api_timeout, args.max_output_tokens, force=args.force_transcribe)
    elif suffix == ".json":
        summary_path = summary_path_for_transcript(source, paths.summarized)
        if args.force_transcribe or not summary_path.exists():
            summarize_transcript_to_markdown(source, summary_path, args.api_timeout, args.max_output_tokens)
        else:
            print(f"summary exists: {summary_path}")
    else:
        raise DesktopError(
            f"unsupported --transcribe-file extension {source.suffix!r}; expected .rec, .fly, .wav, .mp3, or .json"
        )


def transcribe_all_missing(paths: DbPaths, args: argparse.Namespace) -> None:
    audio_files = sorted(
        path for path in paths.audio.iterdir() if path.is_file() and is_transcribable_audio_file(path.name)
    )
    if not audio_files:
        print(f"no WAV or MP3 files found in {paths.audio}")
        return

    for audio_path in audio_files:
        transcript_path = transcript_path_for_audio(audio_path, paths.transcribed)
        summary_path = summary_path_for_stem(audio_path.stem, paths.summarized)
        if not args.force_transcribe and transcript_path.exists() and summary_path.exists():
            continue
        transcribe_and_summarize_audio(audio_path, paths, args.api_timeout, args.max_output_tokens, force=args.force_transcribe)


def sync_device_files(base_url: str, paths: DbPaths, key: Optional[bytes], args: argparse.Namespace) -> list[Path]:
    remote_files = list_remote_files(base_url, args.device_timeout)
    remote_recording_files = [name for name in remote_files if is_audio_file(name)]
    downloaded_audio: list[Path] = []

    suffix_counts = {
        suffix: sum(1 for name in remote_recording_files if Path(name).suffix.lower() == suffix)
        for suffix in sorted(thefly_audio_decryptor.AUDIO_SUFFIXES)
    }
    suffix_summary = ", ".join(f"{count} {suffix}" for suffix, count in suffix_counts.items())
    print(f"device files: {len(remote_files)} total, {suffix_summary}")

    for remote_name in remote_recording_files:
        local_name = safe_local_name(remote_name)
        suffix = Path(local_name).suffix.lower()
        local_path = paths.audio / local_name

        if suffix in thefly_audio_decryptor.ENCRYPTED_AUDIO_SUFFIXES:
            audio_path = output_audio_path_for(Path(local_name), paths.audio)
            processed = False

            if not local_path.exists():
                print(f"downloading {remote_name} -> {local_path}")
                download_remote_file(base_url, remote_name, local_path, args.device_timeout)
                processed = True
            else:
                print(f"already local: {local_path.name}")

            if not audio_path.exists():
                print(f"decoding {local_path.name} -> {audio_path}")
                decode_audio_file(local_path, audio_path, key, args.gap_threshold_ms)
                processed = True
            else:
                print(f"already decoded: {audio_path.name}")

            if processed and audio_path.exists():
                downloaded_audio.append(audio_path)

            if args.clean and local_path.exists():
                print(f"deleting device copy: {remote_name}")
                delete_remote_file(base_url, remote_name, args.device_timeout)
            continue

        if not local_path.exists():
            print(f"downloading {remote_name} -> {local_path}")
            download_remote_file(base_url, remote_name, local_path, args.device_timeout)
            downloaded_audio.append(local_path)
        else:
            print(f"already local: {local_path.name}")

        if args.clean and local_path.exists():
            print(f"deleting device copy: {remote_name}")
            delete_remote_file(base_url, remote_name, args.device_timeout)

    return downloaded_audio


def dns_encode_name(name: str) -> bytes:
    labels = name.rstrip(".").split(".")
    encoded = bytearray()
    for label in labels:
        label_bytes = label.encode("utf-8")
        if len(label_bytes) > 63:
            raise DesktopError(f"DNS label is too long: {label}")
        encoded.append(len(label_bytes))
        encoded.extend(label_bytes)
    encoded.append(0)
    return bytes(encoded)


def dns_read_name(data: bytes, offset: int) -> tuple[str, int]:
    labels: list[str] = []
    cursor = offset
    next_offset = offset
    jumped = False
    seen_offsets: set[int] = set()

    while True:
        if cursor >= len(data):
            raise DesktopError("truncated mDNS name")
        length = data[cursor]
        if length & 0xC0 == 0xC0:
            if cursor + 1 >= len(data):
                raise DesktopError("truncated mDNS compression pointer")
            pointer = ((length & 0x3F) << 8) | data[cursor + 1]
            if pointer in seen_offsets:
                raise DesktopError("mDNS compression pointer loop")
            seen_offsets.add(pointer)
            if not jumped:
                next_offset = cursor + 2
                jumped = True
            cursor = pointer
            continue

        cursor += 1
        if length == 0:
            if not jumped:
                next_offset = cursor
            break
        if length & 0xC0:
            raise DesktopError("unsupported mDNS label encoding")
        if cursor + length > len(data):
            raise DesktopError("truncated mDNS label")
        labels.append(data[cursor : cursor + length].decode("utf-8", errors="replace"))
        cursor += length

    return ".".join(labels), next_offset


def dns_skip_questions(data: bytes, offset: int, count: int) -> int:
    cursor = offset
    for _ in range(count):
        _name, cursor = dns_read_name(data, cursor)
        cursor += 4
        if cursor > len(data):
            raise DesktopError("truncated mDNS question")
    return cursor


def dns_parse_records(data: bytes) -> list[DnsRecord]:
    if len(data) < 12:
        raise DesktopError("truncated mDNS packet")

    _ident, _flags, qdcount, ancount, nscount, arcount = struct.unpack_from("!HHHHHH", data, 0)
    cursor = dns_skip_questions(data, 12, qdcount)
    records: list[DnsRecord] = []

    for _ in range(ancount + nscount + arcount):
        name, cursor = dns_read_name(data, cursor)
        if cursor + 10 > len(data):
            raise DesktopError("truncated mDNS record header")
        record_type, record_class, ttl, rdlength = struct.unpack_from("!HHIH", data, cursor)
        cursor += 10
        rdata_offset = cursor
        cursor += rdlength
        if cursor > len(data):
            raise DesktopError("truncated mDNS record data")
        records.append(DnsRecord(name, record_type, record_class & 0x7FFF, ttl, rdata_offset, data[rdata_offset:cursor]))

    return records


def mdns_query(name: str, record_type: int, timeout: float) -> list[tuple[bytes, DnsRecord]]:
    question = dns_encode_name(name) + struct.pack("!HH", record_type, DNS_CLASS_QU | DNS_CLASS_IN)
    packet = struct.pack("!HHHHHH", 0, 0, 1, 0, 0, 0) + question
    records: list[tuple[bytes, DnsRecord]] = []

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP) as sock:
        sock.settimeout(timeout)
        sock.sendto(packet, (MDNS_GROUP, MDNS_PORT))
        while True:
            try:
                data, _addr = sock.recvfrom(9000)
            except socket.timeout:
                break
            try:
                records.extend((data, record) for record in dns_parse_records(data))
            except DesktopError as exc:
                warn(str(exc))

    return records


def normalized_dns_name(name: str) -> str:
    return name.rstrip(".").lower()


def parse_ptr(data: bytes, record: DnsRecord) -> str:
    name, _offset = dns_read_name(data, record.rdata_offset)
    return name


def parse_srv(data: bytes, record: DnsRecord) -> tuple[int, str]:
    if len(record.rdata) < 6:
        raise DesktopError("truncated mDNS SRV record")
    _priority, _weight, port = struct.unpack_from("!HHH", record.rdata, 0)
    target, _offset = dns_read_name(data, record.rdata_offset + 6)
    return port, target


def parse_address(record: DnsRecord) -> Optional[str]:
    try:
        if record.record_type == DNS_TYPE_A and len(record.rdata) == 4:
            return str(ipaddress.IPv4Address(record.rdata))
        if record.record_type == DNS_TYPE_AAAA and len(record.rdata) == 16:
            return str(ipaddress.IPv6Address(record.rdata))
    except ValueError:
        return None
    return None


def discover_the_fly(timeout: float) -> str:
    ptr_results = mdns_query(THE_FLY_SERVICE, DNS_TYPE_PTR, timeout)
    instance_names: list[str] = []
    all_records = list(ptr_results)

    for data, record in ptr_results:
        if record.record_type == DNS_TYPE_PTR and normalized_dns_name(record.name) == normalized_dns_name(THE_FLY_SERVICE):
            instance = parse_ptr(data, record)
            if instance not in instance_names:
                instance_names.append(instance)

    for instance in list(instance_names):
        all_records.extend(mdns_query(instance, DNS_TYPE_SRV, timeout))

    for instance in instance_names:
        instance_key = normalized_dns_name(instance)
        service: Optional[MdnsService] = None
        for data, record in all_records:
            if record.record_type != DNS_TYPE_SRV or normalized_dns_name(record.name) != instance_key:
                continue
            port, target = parse_srv(data, record)
            target_key = normalized_dns_name(target)

            address = ""
            for _addr_data, addr_record in all_records:
                if normalized_dns_name(addr_record.name) == target_key:
                    parsed_address = parse_address(addr_record)
                    if parsed_address:
                        address = parsed_address
                        break

            if not address:
                all_records.extend(mdns_query(target, DNS_TYPE_A, timeout))
                for _addr_data, addr_record in all_records:
                    if normalized_dns_name(addr_record.name) == target_key:
                        parsed_address = parse_address(addr_record)
                        if parsed_address:
                            address = parsed_address
                            break

            service = MdnsService(instance=instance, target=target, port=port, address=address or target.rstrip("."))
            break

        if service:
            host = service.address
            if ":" in host and not host.startswith("["):
                host = f"[{host}]"
            url = f"http://{host}:{service.port}"
            print(f"mDNS found {service.instance} at {url}")
            return url

    raise DesktopError("could not discover The-Fly via mDNS service _the-fly._tcp.local")


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Sync and process The-Fly recordings on this desktop")
    parser.add_argument("--device", "--url", dest="device", help="The-Fly device IP, hostname, or base URL; otherwise mDNS is used")
    parser.add_argument("--key", type=Path, help="filecrypt key file for encrypted .rec and .fly files")
    parser.add_argument("--password", help="derive the filecrypt key from this password for this session")
    parser.add_argument("--db-dir", type=Path, default=DEFAULT_DB_DIR, help=f"database directory; default: {DEFAULT_DB_DIR}")
    parser.add_argument("--clean", action="store_true", help="delete device files after a local audio copy exists")
    parser.add_argument("--transcribe", action="store_true", help="transcribe and summarize audio files downloaded in this session")
    parser.add_argument(
        "--transcribe-file",
        type=Path,
        action="append",
        help="process one local .rec, .fly, .wav, .mp3, or .json file",
    )
    parser.add_argument(
        "--transcribe-all",
        action="store_true",
        help="process missing transcriptions/summaries for local audio/*.wav and audio/*.mp3 files",
    )
    parser.add_argument("--force-transcribe", action="store_true", help="overwrite existing transcription and summary outputs")
    parser.add_argument("--device-timeout", type=float, default=DEFAULT_DEVICE_TIMEOUT_SECONDS, help="device HTTP timeout in seconds")
    parser.add_argument("--mdns-timeout", type=float, default=DEFAULT_MDNS_TIMEOUT_SECONDS, help="mDNS discovery timeout in seconds")
    parser.add_argument("--api-timeout", type=float, default=DEFAULT_API_TIMEOUT_SECONDS, help="OpenAI API timeout in seconds")
    parser.add_argument("--max-output-tokens", type=int, default=thefly_summarize.DEFAULT_MAX_OUTPUT_TOKENS)
    parser.add_argument("--gap-threshold-ms", type=float, default=DEFAULT_GAP_THRESHOLD_MS)
    return parser.parse_args(argv)


def should_sync_device(args: argparse.Namespace) -> bool:
    local_only_requested = bool(args.transcribe_file) or args.transcribe_all
    return bool(args.device) or args.clean or args.transcribe or not local_only_requested


def can_continue_without_device(args: argparse.Namespace) -> bool:
    return not args.device and not args.clean and not args.transcribe and (bool(args.transcribe_file) or args.transcribe_all)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)

    if args.device_timeout <= 0:
        print("error: --device-timeout must be positive", file=sys.stderr)
        return 2
    if args.mdns_timeout <= 0:
        print("error: --mdns-timeout must be positive", file=sys.stderr)
        return 2
    if args.api_timeout <= 0:
        print("error: --api-timeout must be positive", file=sys.stderr)
        return 2
    if args.max_output_tokens <= 0:
        print("error: --max-output-tokens must be positive", file=sys.stderr)
        return 2
    if args.gap_threshold_ms < 0:
        print("error: --gap-threshold-ms must be non-negative", file=sys.stderr)
        return 2

    try:
        key = load_filecrypt_key(args.key, args.password)
        paths = default_db_paths(args.db_dir)
        ensure_db_dirs(paths)
        print(f"database: {paths.root}")

        downloaded_audio: list[Path] = []
        if should_sync_device(args):
            try:
                base_url = normalize_device_base(args.device) if args.device else discover_the_fly(args.mdns_timeout)
                print(f"device: {base_url}")
                downloaded_audio = sync_device_files(base_url, paths, key, args)
            except DesktopError:
                if can_continue_without_device(args):
                    warn("device sync skipped because the device is not reachable")
                else:
                    raise

        if args.transcribe:
            if not downloaded_audio:
                print("no newly downloaded audio files to transcribe")
            for audio_path in downloaded_audio:
                transcribe_and_summarize_audio(audio_path, paths, args.api_timeout, args.max_output_tokens, force=args.force_transcribe)

        if args.transcribe_file:
            for input_path in args.transcribe_file:
                handle_transcribe_file(input_path, paths, key, args)

        if args.transcribe_all:
            transcribe_all_missing(paths, args)
    except (DesktopError, OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
