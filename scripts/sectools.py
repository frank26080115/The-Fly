#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import re
import sys
from pathlib import Path
from typing import Optional, Sequence

from cryptography.exceptions import InvalidTag
from cryptography.hazmat.primitives.ciphers.aead import AESGCM


FILECRYPT_KEY_SIZE = 32
FILE_PACKET_HEADER_MAGIC = 0xDEADBEEF
FILE_PACKET_PAYLOAD_MAX = 256
FILE_PACKET_PAYLOAD_BYTES = FILE_PACKET_PAYLOAD_MAX * 2
GCM_NONCE_SIZE = 12
GCM_TAG_SIZE = 16

SEC0_HEADER_SIZE = 20
SEC1_HEADER_SIZE = 28
SEC0_PACKET_SIZE = SEC0_HEADER_SIZE + FILE_PACKET_PAYLOAD_BYTES
SEC1_PACKET_SIZE = SEC1_HEADER_SIZE + FILE_PACKET_PAYLOAD_BYTES
ENCRYPTED_PACKET_SIZE = GCM_NONCE_SIZE + SEC1_PACKET_SIZE + GCM_TAG_SIZE

SALT_FILECRYPT = bytes([0x98, 0xC2, 0x5A, 0xF2, 0xB7, 0x0F, 0xA4, 0xB3, 0x42, 0xB4, 0x64, 0xE5, 0xEE, 0xD6, 0xFF, 0x3D, 0x0D, 0xD8, 0x21, 0x9C, 0x9D, 0x7B, 0x16, 0xB4, 0xCE, 0xDE, 0xCF, 0xFA, 0xCA, 0x4E, 0xF3, 0x2F])
PBKDF_ITERATIONS = 100000


class SecurityToolError(ValueError):
    pass


def derive_filecrypt_key(password: str) -> bytes:
    return hashlib.pbkdf2_hmac(
        "sha256",
        password.encode("utf-8"),
        SALT_FILECRYPT,
        PBKDF_ITERATIONS,
        dklen=FILECRYPT_KEY_SIZE,
    )


def read_key_file(path: Path) -> bytes:
    data = path.read_bytes()
    if len(data) == FILECRYPT_KEY_SIZE:
        return data

    if len(data) >= FILECRYPT_KEY_SIZE * 2:
        text = data.decode("utf-8", errors="ignore")
        text = re.sub(r"0x", "", text, flags=re.IGNORECASE)
        text = re.sub(r"[^A-Za-z0-9]", "", text)
        if len(text) < FILECRYPT_KEY_SIZE * 2:
            raise SecurityToolError(f"hex key file is too short after cleanup: {path}")

        hex_text = text[: FILECRYPT_KEY_SIZE * 2]
        if not re.fullmatch(r"[0-9A-Fa-f]{64}", hex_text):
            raise SecurityToolError(f"hex key file does not begin with 64 hexadecimal characters after cleanup: {path}")

        return bytes.fromhex(hex_text)

    raise SecurityToolError(f"invalid key file size {len(data)} bytes for {path}; expected 32 raw bytes or 64+ hex text bytes")


def encrypted_recording_packet_count(input_path: Path) -> int:
    size = input_path.stat().st_size
    if size == 0:
        raise SecurityToolError(f"input recording is empty: {input_path}")
    if size % ENCRYPTED_PACKET_SIZE:
        raise SecurityToolError(
            f"encrypted recording size {size} is not a multiple of encrypted packet size {ENCRYPTED_PACKET_SIZE}: {input_path}"
        )
    return size // ENCRYPTED_PACKET_SIZE


def _strip_security_packet_nonce_fields(plaintext: bytes) -> bytes:
    if len(plaintext) != SEC1_PACKET_SIZE:
        raise SecurityToolError(f"decrypted packet has invalid size {len(plaintext)}; expected {SEC1_PACKET_SIZE}")

    magic = int.from_bytes(plaintext[:4], "little")
    if magic != FILE_PACKET_HEADER_MAGIC:
        raise SecurityToolError("decrypted packet has invalid file packet magic")

    return plaintext[:14] + plaintext[22:]


def decrypt_recording_file(input_path: Path, output_path: Path, key: bytes) -> int:
    if len(key) != FILECRYPT_KEY_SIZE:
        raise SecurityToolError(f"AES-GCM filecrypt key must be {FILECRYPT_KEY_SIZE} bytes")

    packet_count = encrypted_recording_packet_count(input_path)
    aesgcm = AESGCM(key)

    with input_path.open("rb") as src, output_path.open("wb") as dst:
        for index in range(packet_count):
            offset = index * ENCRYPTED_PACKET_SIZE
            packet = src.read(ENCRYPTED_PACKET_SIZE)
            if len(packet) != ENCRYPTED_PACKET_SIZE:
                raise SecurityToolError(f"truncated encrypted packet at byte {offset}")

            nonce = packet[:GCM_NONCE_SIZE]
            ciphertext = packet[GCM_NONCE_SIZE : GCM_NONCE_SIZE + SEC1_PACKET_SIZE]
            tag = packet[GCM_NONCE_SIZE + SEC1_PACKET_SIZE :]

            try:
                plaintext = aesgcm.decrypt(nonce, ciphertext + tag, None)
            except InvalidTag as exc:
                raise SecurityToolError(f"AES-GCM authentication failed for packet {index} at byte {offset}") from exc

            dst.write(_strip_security_packet_nonce_fields(plaintext))

    return packet_count


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="The Fly security helper tools")
    parser.add_argument("output", type=Path, help="output .key file")
    parser.add_argument("-p", "--password", required=True, help="password used to derive the filecrypt key")
    parser.add_argument("--hex", action="store_true", help="write the key as lowercase hexadecimal text instead of raw binary")
    parser.add_argument("-f", "--force", action="store_true", help="overwrite an existing output file")
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    output_path: Path = args.output

    if output_path.exists() and not args.force:
        print(f"error: output file exists; use --force to overwrite: {output_path}", file=sys.stderr)
        return 2

    key = derive_filecrypt_key(args.password)
    data = key.hex().encode("ascii") + b"\n" if args.hex else key
    try:
        output_path.write_bytes(data)
    except OSError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"wrote {output_path} ({'hex text' if args.hex else 'raw binary'}, {len(data)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
