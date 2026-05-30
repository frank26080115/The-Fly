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
from typing import Any, Optional, Sequence

import sectools
import thefly_audio_decoder
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

DIR_RAW_REC = "raw_rec"
DIR_WAV = "wav"
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
    raw_rec: Path
    wav: Path
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
        raw_rec=root / DIR_RAW_REC,
        wav=root / DIR_WAV,
        transcribed=root / DIR_TRANSCRIBED,
        summarized=root / DIR_SUMMARIZED,
    )


def ensure_db_dirs(paths: DbPaths) -> None:
    for directory in (paths.root, paths.raw_rec, paths.wav, paths.transcribed, paths.summarized):
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


def is_rec_file(file_name: str) -> bool:
    return Path(file_name).suffix.lower() == ".rec"


def is_wav_file(file_name: str) -> bool:
    return Path(file_name).suffix.lower() == ".wav"


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


def save_wav_directly(source_path: Path, wav_path: Path) -> Path:
    wav_path.parent.mkdir(parents=True, exist_ok=True)
    if source_path.resolve() == wav_path.resolve():
        return wav_path

    temp_path = wav_path.with_name(wav_path.name + ".download")
    try:
        shutil.copy2(source_path, temp_path)
        temp_path.replace(wav_path)
    finally:
        try:
            temp_path.unlink()
        except FileNotFoundError:
            pass
    return wav_path


def delete_remote_file(base_url: str, remote_name: str, timeout: float) -> None:
    urlopen_bytes(endpoint_url(base_url, "/delete_file", {"file_name": remote_name}), timeout)


def load_filecrypt_key(key_file: Optional[Path], password: Optional[str]) -> Optional[bytes]:
    if key_file is not None and password is not None:
        raise DesktopError("specify either --key or --password, not both")
    if key_file is not None:
        return sectools.read_key_file(key_file.expanduser())
    if password is not None:
        return sectools.derive_filecrypt_key(password)
    return None


def decode_rec_to_wav(rec_path: Path, wav_path: Path, key: Optional[bytes], gap_threshold_ms: float) -> None:
    wav_path.parent.mkdir(parents=True, exist_ok=True)

    pcm_temp = tempfile.NamedTemporaryFile(prefix="thefly-", suffix=".pcm", dir=str(wav_path.parent), delete=False)
    pcm_path = Path(pcm_temp.name)
    pcm_temp.close()

    decrypt_temp_path: Optional[Path] = None
    decode_input_path = rec_path

    try:
        if key is not None:
            decrypt_temp = tempfile.NamedTemporaryFile(prefix="thefly-decrypted-", suffix=".rec", delete=False)
            decrypt_temp_path = Path(decrypt_temp.name)
            decrypt_temp.close()
            packet_count = sectools.decrypt_recording_file(rec_path, decrypt_temp_path, key)
            print(f"decrypted packets: {packet_count} ({rec_path.name})")
            decode_input_path = decrypt_temp_path

        thefly_audio_decoder.decode_recording(decode_input_path, pcm_path, wav_path, gap_threshold_ms)
    finally:
        for temp_path in (pcm_path, decrypt_temp_path):
            if temp_path is not None:
                try:
                    temp_path.unlink()
                except FileNotFoundError:
                    pass


def transcript_path_for_wav(wav_path: Path, transcribed_dir: Path) -> Path:
    return transcribed_dir / f"{wav_path.stem}.trans.json"


def summary_path_for_stem(stem: str, summarized_dir: Path) -> Path:
    return summarized_dir / f"{stem}.sum.md"


def summary_path_for_transcript(transcript_path: Path, summarized_dir: Path) -> Path:
    name = transcript_path.name
    if name.lower().endswith(".trans.json"):
        stem = name[: -len(".trans.json")]
    else:
        stem = transcript_path.stem
    return summary_path_for_stem(stem, summarized_dir)


def transcribe_wav_to_json(wav_path: Path, output_path: Path, timeout: float) -> Path:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    result = thefly_transcription.transcribe_wav(
        input_path=wav_path,
        model=thefly_transcription.DEFAULT_MODEL,
        response_format="diarized_json",
        language=None,
        prompt=None,
        api_url=thefly_transcription.DEFAULT_API_URL,
        timeout=timeout,
        chunking_strategy="auto",
    )
    thefly_transcription.add_recording_metadata(result, wav_path)
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


def transcribe_and_summarize_wav(wav_path: Path, paths: DbPaths, timeout: float, max_output_tokens: int, force: bool = False) -> None:
    transcript_path = transcript_path_for_wav(wav_path, paths.transcribed)
    if force or not transcript_path.exists():
        transcribe_wav_to_json(wav_path, transcript_path, timeout)
    else:
        print(f"transcription exists: {transcript_path}")

    summary_path = summary_path_for_stem(wav_path.stem, paths.summarized)
    if force or not summary_path.exists():
        summarize_transcript_to_markdown(transcript_path, summary_path, timeout, max_output_tokens)
    else:
        print(f"summary exists: {summary_path}")


def handle_transcribe_file(input_path: Path, paths: DbPaths, key: Optional[bytes], args: argparse.Namespace) -> None:
    source = input_path.expanduser()
    if not source.exists() or not source.is_file():
        raise DesktopError(f"--transcribe-file is not a file: {source}")

    suffix = source.suffix.lower()
    if suffix == ".rec":
        wav_path = paths.wav / f"{source.stem}.wav"
        decode_rec_to_wav(source, wav_path, key, args.gap_threshold_ms)
        transcribe_and_summarize_wav(wav_path, paths, args.api_timeout, args.max_output_tokens, force=args.force_transcribe)
    elif suffix == ".wav":
        wav_path = save_wav_directly(source, paths.wav / source.name)
        transcribe_and_summarize_wav(wav_path, paths, args.api_timeout, args.max_output_tokens, force=args.force_transcribe)
    elif suffix == ".json":
        summary_path = summary_path_for_transcript(source, paths.summarized)
        if args.force_transcribe or not summary_path.exists():
            summarize_transcript_to_markdown(source, summary_path, args.api_timeout, args.max_output_tokens)
        else:
            print(f"summary exists: {summary_path}")
    else:
        raise DesktopError(f"unsupported --transcribe-file extension {source.suffix!r}; expected .rec, .wav, or .json")


def transcribe_all_missing(paths: DbPaths, args: argparse.Namespace) -> None:
    wav_files = sorted(paths.wav.glob("*.wav"))
    if not wav_files:
        print(f"no WAV files found in {paths.wav}")
        return

    for wav_path in wav_files:
        transcript_path = transcript_path_for_wav(wav_path, paths.transcribed)
        summary_path = summary_path_for_stem(wav_path.stem, paths.summarized)
        if not args.force_transcribe and transcript_path.exists() and summary_path.exists():
            continue
        transcribe_and_summarize_wav(wav_path, paths, args.api_timeout, args.max_output_tokens, force=args.force_transcribe)


def sync_device_files(base_url: str, paths: DbPaths, key: Optional[bytes], args: argparse.Namespace) -> list[Path]:
    remote_files = list_remote_files(base_url, args.device_timeout)
    remote_rec_files = [name for name in remote_files if is_rec_file(name)]
    remote_wav_files = [name for name in remote_files if is_wav_file(name)]
    remote_recording_files = remote_rec_files + remote_wav_files
    downloaded_wavs: list[Path] = []

    print(f"device files: {len(remote_files)} total, {len(remote_rec_files)} .rec, {len(remote_wav_files)} .wav")

    for remote_name in remote_recording_files:
        local_name = safe_local_name(remote_name)
        suffix = Path(local_name).suffix.lower()

        if suffix == ".rec":
            raw_path = paths.raw_rec / local_name
            wav_path = paths.wav / f"{Path(local_name).stem}.wav"
            processed = False

            if not raw_path.exists():
                print(f"downloading {remote_name} -> {raw_path}")
                download_remote_file(base_url, remote_name, raw_path, args.device_timeout)
                processed = True
            else:
                print(f"already local: {raw_path.name}")

            if not wav_path.exists():
                print(f"decoding {raw_path.name} -> {wav_path}")
                decode_rec_to_wav(raw_path, wav_path, key, args.gap_threshold_ms)
                processed = True

            if processed and wav_path.exists():
                downloaded_wavs.append(wav_path)

            if args.clean and raw_path.exists():
                print(f"deleting device copy: {remote_name}")
                delete_remote_file(base_url, remote_name, args.device_timeout)
            continue

        wav_path = paths.wav / local_name
        if not wav_path.exists():
            print(f"downloading {remote_name} -> {wav_path}")
            download_remote_file(base_url, remote_name, wav_path, args.device_timeout)
            downloaded_wavs.append(wav_path)
        else:
            print(f"already local: {wav_path.name}")

        if args.clean and wav_path.exists():
            print(f"deleting device copy: {remote_name}")
            delete_remote_file(base_url, remote_name, args.device_timeout)

    return downloaded_wavs


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
    parser.add_argument("--key", type=Path, help="filecrypt key file for encrypted .rec files")
    parser.add_argument("--password", help="derive the filecrypt key from this password for this session")
    parser.add_argument("--db-dir", type=Path, default=DEFAULT_DB_DIR, help=f"database directory; default: {DEFAULT_DB_DIR}")
    parser.add_argument("--clean", action="store_true", help="delete device files after a local raw_rec copy exists")
    parser.add_argument("--transcribe", action="store_true", help="transcribe and summarize WAV files downloaded in this session")
    parser.add_argument("--transcribe-file", type=Path, action="append", help="process one local .rec, .wav, or .json file")
    parser.add_argument("--transcribe-all", action="store_true", help="process missing transcriptions/summaries for local wav/*.wav files")
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

        downloaded_wavs: list[Path] = []
        if should_sync_device(args):
            try:
                base_url = normalize_device_base(args.device) if args.device else discover_the_fly(args.mdns_timeout)
                print(f"device: {base_url}")
                downloaded_wavs = sync_device_files(base_url, paths, key, args)
            except DesktopError:
                if can_continue_without_device(args):
                    warn("device sync skipped because the device is not reachable")
                else:
                    raise

        if args.transcribe:
            if not downloaded_wavs:
                print("no newly downloaded WAV files to transcribe")
            for wav_path in downloaded_wavs:
                transcribe_and_summarize_wav(wav_path, paths, args.api_timeout, args.max_output_tokens, force=args.force_transcribe)

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
