#!/usr/bin/env python3
import argparse
import os
import subprocess
import sys
from pathlib import Path


DEFAULT_ENV = "thefly_sec0"


def project_root() -> Path:
    # tools/just_upload_it.py -> project root
    return Path(__file__).resolve().parents[1]


def platformio_home() -> Path:
    return Path.home() / ".platformio"


def esptool_path() -> Path:
    exe = platformio_home() / "packages" / "tool-esptool" / "esptool.exe"
    if not exe.exists():
        raise FileNotFoundError(f"Could not find esptool at: {exe}")
    return exe


def build_dir(env: str) -> Path:
    return project_root() / ".pio" / "build" / env


def require_file(path: Path, name: str) -> Path:
    if not path.exists():
        raise FileNotFoundError(f"Missing {name}: {path}")
    return path


def detect_port_with_pio(env: str) -> str:
    cmd = ["pio", "device", "list", "--json-output"]
    result = subprocess.run(
        cmd,
        cwd=project_root(),
        text=True,
        capture_output=True,
        check=True,
    )

    import json
    devices = json.loads(result.stdout)

    if not devices:
        raise RuntimeError("No serial devices found.")

    # Prefer CP210x / CH340 / USB serial-ish devices when possible
    preferred_keywords = ["cp210", "ch340", "usb", "uart", "serial", "silicon labs"]

    for dev in devices:
        haystack = " ".join(
            str(dev.get(k, "")) for k in ["description", "hwid", "manufacturer"]
        ).lower()
        if any(k in haystack for k in preferred_keywords):
            return dev["port"]

    return devices[0]["port"]


def detect_usb_serial_port() -> str:
    import json

    result = subprocess.run(
        ["pio", "device", "list", "--json-output"],
        cwd=project_root(),
        text=True,
        capture_output=True,
        check=True,
    )

    devices = json.loads(result.stdout)

    usb_devices = []
    preferred = []

    for dev in devices:
        port = dev.get("port", "")
        blob = " ".join(str(dev.get(k, "")) for k in (
            "description",
            "hwid",
            "manufacturer",
            "product",
        )).lower()

        if "usb" in blob or "vid:" in blob or "pid:" in blob:
            usb_devices.append(dev)

        if any(x in blob for x in (
            "cp210",
            "ch340",
            "ch910",
            "ftdi",
            "silicon labs",
            "usb serial",
            "usb jtag",
            "esp32",
            "espressif",
        )):
            preferred.append(dev)

    matches = preferred or usb_devices

    if not matches:
        raise RuntimeError("No USB-backed serial COM port found.")

    if len(matches) > 1:
        print("⚠️ Multiple USB serial devices found; using the first one:")
        for dev in matches:
            print(f"  {dev.get('port')} - {dev.get('description', '')}")

    return matches[0]["port"]


def upload(env: str, port_name: str) -> None:
    bdir = build_dir(env)

    bootloader = require_file(bdir / "bootloader.bin", "bootloader.bin")
    partitions = require_file(bdir / "partitions.bin", "partitions.bin")
    firmware = require_file(bdir / "firmware.bin", "firmware.bin")

    #port = detect_port_with_pio(env) if port_name == "auto" else port_name
    port = detect_usb_serial_port() if port_name == "auto" else port_name

    cmd = [
        str(Path.home() / ".platformio" / "penv" / "Scripts" / "python.exe"),
        str(Path.home() / ".platformio" / "packages" / "tool-esptoolpy" / "esptool.py"),
        "--chip", "esp32",
        "--port", port,
        "--baud", "921600",
        "--before", "default_reset",
        "--after", "hard_reset",
        "write_flash",
        "-z",
        "--flash_mode", "dio",
        "--flash_freq", "80m",
        "--flash_size", "16MB",
        "0x1000", str(bootloader),
        "0x8000", str(partitions),
        "0x10000", str(firmware),
    ]

    print("🚀 Uploading without rebuilding...")
    print(" ".join(f'"{x}"' if " " in x else x for x in cmd))
    subprocess.run(cmd, cwd=project_root(), check=True)


def monitor(env: str, port_name: str) -> None:
    cmd = ["pio", "device", "monitor", "-e", env]

    if port_name != "auto":
        cmd += ["--port", port_name]

    print("📡 Starting PlatformIO monitor...")
    subprocess.run(cmd, cwd=project_root(), check=True)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Upload existing PlatformIO ESP32 build artifacts without rebuilding."
    )

    parser.add_argument(
        "--env",
        default=DEFAULT_ENV,
        help=f"PlatformIO environment name. Default: {DEFAULT_ENV}",
    )

    parser.add_argument(
        "--and-monitor",
        action="store_true",
        help="Start PlatformIO serial monitor after uploading.",
    )

    parser.add_argument(
        "--port-name",
        default="auto",
        help='Serial port name, like COM7. Default: "auto".',
    )

    args = parser.parse_args()

    try:
        port = detect_usb_serial_port() if args.port_name == "auto" else args.port_name
        upload(args.env, port)

        if args.and_monitor:
            monitor(args.env, port)

    except subprocess.CalledProcessError as e:
        print(f"❌ Command failed with exit code {e.returncode}", file=sys.stderr)
        return e.returncode

    except Exception as e:
        print(f"❌ {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
