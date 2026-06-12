#!/usr/bin/env python3
from __future__ import annotations

"""
Build scripts/thefly_desktop.py as a Windows executable.

Typical use:
    python -m pip install pyinstaller -r scripts/requirements.txt
    python scripts/setup.py

The default build uses PyInstaller one-file mode so the distributable output is
dist/thefly-desktop.exe. Native libraries are packed into that exe, though
PyInstaller still extracts them to a temporary directory when the program runs.
"""

import argparse
import importlib.metadata
import importlib.util
import subprocess
import sys
from pathlib import Path
from typing import Sequence


SCRIPTS_DIR = Path(__file__).resolve().parent
ROOT_DIR = SCRIPTS_DIR.parent
ENTRY_SCRIPT = SCRIPTS_DIR / "thefly_desktop.py"
DEFAULT_EXE_NAME = "thefly-desktop"

BUILD_DIR = ROOT_DIR / "build" / "pyinstaller"
DIST_DIR = ROOT_DIR / "dist"

LOCAL_HIDDEN_IMPORTS = (
    "audio_file_shortener",
    "models",
    "sectools",
    "thefly_audio_decryptor",
    "thefly_summarize",
    "thefly_transcription",
)

CRYPTO_HIDDEN_IMPORTS = (
    "cryptography",
    "cryptography.exceptions",
    "cryptography.hazmat.primitives.ciphers.aead",
)

DEFAULT_AI_PACKAGES = (
    ("faster_whisper", "faster-whisper"),
    ("ctranslate2", "ctranslate2"),
    ("av", "av"),
    ("tokenizers", "tokenizers"),
    ("huggingface_hub", "huggingface-hub"),
)

OPENAI_WHISPER_PACKAGES = (
    ("whisper", "openai-whisper"),
    ("tiktoken", "tiktoken"),
    ("torch", "torch"),
)


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build The-Fly desktop as a Windows exe")
    parser.add_argument(
        "command",
        nargs="?",
        default="build",
        choices=("build",),
        help="build the executable; default: build",
    )
    parser.add_argument("--name", default=DEFAULT_EXE_NAME, help=f"exe base name; default: {DEFAULT_EXE_NAME}")
    parser.add_argument("--onedir", action="store_true", help="build a folder instead of a single exe")
    parser.add_argument("--dist-dir", type=Path, default=DIST_DIR, help=f"output directory; default: {DIST_DIR}")
    parser.add_argument("--work-dir", type=Path, default=BUILD_DIR, help=f"PyInstaller work directory; default: {BUILD_DIR}")
    parser.add_argument("--no-clean", dest="clean", action="store_false", help="reuse PyInstaller's previous work files")
    parser.add_argument("--use-upx", action="store_true", help="allow UPX compression if PyInstaller finds UPX")
    parser.add_argument(
        "--include-openai-whisper",
        action="store_true",
        help="also bundle the optional openai-whisper backend; this can make the exe very large",
    )
    parser.add_argument("--dry-run", action="store_true", help="print the PyInstaller command without running it")
    parser.add_argument(
        "pyinstaller_args",
        nargs=argparse.REMAINDER,
        help="extra arguments passed to PyInstaller after --",
    )
    parser.set_defaults(clean=True)
    return parser.parse_args(argv)


def module_available(module_name: str) -> bool:
    try:
        return importlib.util.find_spec(module_name) is not None
    except (ImportError, ModuleNotFoundError, ValueError):
        return False


def distribution_available(distribution_name: str) -> bool:
    try:
        importlib.metadata.version(distribution_name)
    except importlib.metadata.PackageNotFoundError:
        return False
    return True


def add_hidden_import(command: list[str], module_name: str) -> None:
    command.extend(("--hidden-import", module_name))


def add_metadata(command: list[str], distribution_name: str) -> None:
    if distribution_available(distribution_name):
        command.extend(("--copy-metadata", distribution_name))


def add_collect_all_if_available(command: list[str], module_name: str, distribution_name: str) -> bool:
    if not module_available(module_name):
        return False

    add_hidden_import(command, module_name)
    command.extend(("--collect-all", module_name))
    add_metadata(command, distribution_name)
    return True


def build_pyinstaller_command(args: argparse.Namespace) -> list[str]:
    if not ENTRY_SCRIPT.exists():
        raise SystemExit(f"entry script not found: {ENTRY_SCRIPT}")

    command = [
        sys.executable,
        "-m",
        "PyInstaller",
        "--noconfirm",
        "--console",
        "--name",
        args.name,
        "--distpath",
        str(args.dist_dir),
        "--workpath",
        str(args.work_dir),
        "--specpath",
        str(args.work_dir),
        "--paths",
        str(SCRIPTS_DIR),
    ]

    command.append("--onedir" if args.onedir else "--onefile")
    if args.clean:
        command.append("--clean")
    if not args.use_upx:
        command.append("--noupx")

    for module_name in LOCAL_HIDDEN_IMPORTS:
        add_hidden_import(command, module_name)

    for module_name in CRYPTO_HIDDEN_IMPORTS:
        if module_available(module_name):
            add_hidden_import(command, module_name)

    missing_default_ai: list[str] = []
    for module_name, distribution_name in DEFAULT_AI_PACKAGES:
        if not add_collect_all_if_available(command, module_name, distribution_name):
            missing_default_ai.append(module_name)

    if args.include_openai_whisper:
        missing_openai_whisper: list[str] = []
        for module_name, distribution_name in OPENAI_WHISPER_PACKAGES:
            if not add_collect_all_if_available(command, module_name, distribution_name):
                missing_openai_whisper.append(module_name)
        if missing_openai_whisper:
            print(
                "warning: optional openai-whisper modules were not found and will not be bundled: "
                + ", ".join(missing_openai_whisper),
                file=sys.stderr,
            )

    if missing_default_ai:
        print(
            "warning: default transcription modules were not found and will not be bundled: "
            + ", ".join(missing_default_ai),
            file=sys.stderr,
        )

    extra_args = list(args.pyinstaller_args)
    if extra_args[:1] == ["--"]:
        extra_args = extra_args[1:]
    command.extend(extra_args)
    command.append(str(ENTRY_SCRIPT))
    return command


def main(argv: Sequence[str] | None = None) -> int:
    sys.path.insert(0, str(SCRIPTS_DIR))
    args = parse_args(argv)

    if sys.platform != "win32":
        print("warning: PyInstaller cannot cross-compile; run this on Windows to produce a Windows exe", file=sys.stderr)

    if not args.dry_run and not module_available("PyInstaller"):
        print("error: PyInstaller is not installed", file=sys.stderr)
        print("install it with: python -m pip install pyinstaller", file=sys.stderr)
        return 2

    command = build_pyinstaller_command(args)
    print(subprocess.list2cmdline(command))
    if args.dry_run:
        return 0

    completed = subprocess.run(command, cwd=ROOT_DIR)
    if completed.returncode:
        return completed.returncode

    output_path = args.dist_dir / (args.name + ("" if args.onedir else ".exe"))
    print(f"build output: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
