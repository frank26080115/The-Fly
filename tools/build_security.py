from __future__ import annotations

import argparse
import filecmp
import glob
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from typing import Any


def project_root() -> Path:
    if "__file__" in globals():
        return Path(__file__).resolve().parents[1]
    return Path.cwd().resolve()


ROOT = project_root()
DEFAULT_KEY = ROOT / "secure_boot_signing_key.pem"
SECURE_ENVS = ("thefly_sec1", "thefly_sec2", "thefly_sec3")
DEFAULT_FLASH_BAUD = 921600
SDKCONFIG_REQUIRED_FLAGS = (
    "CONFIG_SECURE_BOOT",
    "CONFIG_SECURE_BOOT_V2_ENABLED",
    "CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES",
    "CONFIG_SECURE_FLASH_ENC_ENABLED",
)


class BuildSecurityError(RuntimeError):
    pass


def parse_sdkconfig_text(text: str) -> dict[str, object]:
    values: dict[str, object] = {}
    unset_re = re.compile(r"^#\s+(CONFIG_[A-Za-z0-9_]+)\s+is not set$")

    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line:
            continue

        unset_match = unset_re.match(line)
        if unset_match:
            values[unset_match.group(1)] = False
            continue

        if not line.startswith("CONFIG_") or "=" not in line:
            continue

        key, value = line.split("=", 1)
        value = value.strip()
        if value == "y":
            values[key] = True
        elif value == "n":
            values[key] = False
        elif len(value) >= 2 and value[0] == value[-1] == '"':
            values[key] = value[1:-1]
        else:
            values[key] = value

    return values


def parse_sdkconfig_file(path: Path) -> dict[str, object]:
    if not path.exists():
        raise BuildSecurityError(f"missing sdkconfig file: {path}")
    return parse_sdkconfig_text(path.read_text(encoding="utf-8", errors="replace"))


def is_enabled(values: dict[str, object], name: str) -> bool:
    return values.get(name) is True


def signing_key_from_values(values: dict[str, object], fallback: Path = DEFAULT_KEY) -> Path:
    configured = values.get("CONFIG_SECURE_BOOT_SIGNING_KEY")
    if isinstance(configured, str) and configured:
        path = Path(configured)
        return path if path.is_absolute() else ROOT / path
    return fallback


def project_sdkconfig_path(pioenv: str) -> Path:
    return ROOT / f"sdkconfig.{pioenv}"


def validate_secure_sdkconfig(pioenv: str) -> tuple[dict[str, object], Path]:
    values = parse_sdkconfig_file(project_sdkconfig_path(pioenv))
    missing = [flag for flag in SDKCONFIG_REQUIRED_FLAGS if not is_enabled(values, flag)]
    if missing:
        raise BuildSecurityError(
            f"{pioenv} sdkconfig is missing required secure flags: {', '.join(missing)}"
        )

    key_path = signing_key_from_values(values)
    if not key_path.exists():
        raise BuildSecurityError(
            f"secure boot signing key is missing: {key_path}\n"
            f"Create it with: python tools\\build_security.py --create-key"
        )
    return values, key_path


def find_espsecure(platformio_env: Any | None = None) -> Path:
    candidates: list[Path] = []

    if platformio_env is not None:
        try:
            package_dir = platformio_env.PioPlatform().get_package_dir("tool-esptoolpy")
            if package_dir:
                candidates.append(Path(package_dir) / "espsecure.py")
        except Exception:
            pass

    for package_root in (ROOT / ".pio" / "packages", Path.home() / ".platformio" / "packages"):
        candidates.extend(package_root.glob("tool-esptoolpy*/espsecure.py"))

    for candidate in candidates:
        if candidate.exists():
            return candidate

    raise BuildSecurityError("could not find PlatformIO's espsecure.py")


def find_esptool(platformio_env: Any | None = None) -> Path:
    candidates: list[Path] = []

    if platformio_env is not None:
        try:
            package_dir = platformio_env.PioPlatform().get_package_dir("tool-esptoolpy")
            if package_dir:
                candidates.append(Path(package_dir) / "esptool.py")
        except Exception:
            pass

    for package_root in (ROOT / ".pio" / "packages", Path.home() / ".platformio" / "packages"):
        candidates.extend(package_root.glob("tool-esptoolpy*/esptool.py"))

    for candidate in candidates:
        if candidate.exists():
            return candidate

    raise BuildSecurityError("could not find PlatformIO's esptool.py")


def candidate_python_executables(platformio_env: Any | None = None) -> list[Path]:
    candidates: list[Path] = []

    # Candidate order, with typical Windows paths:
    # 1. C:\Users\<user>\.platformio\penv\.espidf-5.3.2\Scripts\python.exe
    # 2. PlatformIO's $PYTHONEXE, usually C:\Users\<user>\.platformio\penv\Scripts\python.exe
    # 3. C:\Users\<user>\.platformio\penv\Scripts\python.exe
    # 4. sys.executable from the current process
    # 5. python.exe found on PATH, for example C:\Python313\python.exe
    # 6. C:\Python*\python.exe
    # 7. C:\Users\<user>\AppData\Local\Programs\Python\Python*\python.exe
    # ESP-IDF's PlatformIO venv carries espsecure.py's crypto dependency on this
    # project, while the generic PlatformIO penv often does not.
    candidates.append(Path.home() / ".platformio" / "penv" / ".espidf-5.3.2" / "Scripts" / "python.exe")

    if platformio_env is not None:
        try:
            python = platformio_env.subst("$PYTHONEXE")
            if python:
                candidates.append(Path(python))
        except Exception:
            pass

    candidates.extend(
        (
            Path.home() / ".platformio" / "penv" / "Scripts" / "python.exe",
            Path(sys.executable),
        )
    )

    python_on_path = shutil.which("python")
    if python_on_path:
        candidates.append(Path(python_on_path))

    if os.name == "nt":
        candidates.extend(Path(path) for path in glob.glob(r"C:\Python*\python.exe"))
        local_app_data = os.environ.get("LOCALAPPDATA")
        if local_app_data:
            candidates.extend(
                Path(path)
                for path in glob.glob(
                    str(Path(local_app_data) / "Programs" / "Python" / "Python*" / "python.exe")
                )
            )

    unique: list[Path] = []
    seen: set[str] = set()
    for candidate in candidates:
        key = str(candidate).lower()
        if key not in seen:
            unique.append(candidate)
            seen.add(key)
    return unique


ESPSECURE_IMPORT_MODULES = ("cryptography", "serial", "ecdsa", "reedsolo", "bitstring")
ESPSECURE_PIP_PACKAGES = {
    "serial": "pyserial",
}


class PythonProbe:
    def __init__(self, python: Path, missing_modules: list[str], import_error: str = "") -> None:
        self.python = python
        self.missing_modules = missing_modules
        self.import_error = import_error

    @property
    def can_run_espsecure(self) -> bool:
        return not self.missing_modules and not self.import_error


def missing_espsecure_modules(python: Path) -> list[str]:
    if not python.exists():
        return list(ESPSECURE_IMPORT_MODULES)

    completed = subprocess.run(
        [
            str(python),
            "-c",
            (
                "import importlib.util; "
                f"mods={list(ESPSECURE_IMPORT_MODULES)!r}; "
                "print('\\n'.join(m for m in mods if importlib.util.find_spec(m) is None))"
            ),
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    if completed.returncode != 0:
        return list(ESPSECURE_IMPORT_MODULES)
    return [line.strip() for line in completed.stdout.splitlines() if line.strip()]


def espsecure_import_error(python: Path, espsecure: Path) -> str:
    if not python.exists():
        return f"missing Python executable: {python}"

    completed = subprocess.run(
        [
            str(python),
            "-c",
            (
                "import sys; "
                f"sys.path.insert(0, {str(espsecure.parent)!r}); "
                "import espsecure"
            ),
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode == 0:
        return ""
    return completed.stderr.strip()


def probe_python_for_espsecure(python: Path, espsecure: Path) -> PythonProbe:
    missing_modules = missing_espsecure_modules(python)
    if missing_modules:
        return PythonProbe(python, missing_modules)
    return PythonProbe(python, missing_modules, espsecure_import_error(python, espsecure))


def python_can_run_espsecure(python: Path, espsecure: Path) -> bool:
    return probe_python_for_espsecure(python, espsecure).can_run_espsecure


def python_executable(platformio_env: Any | None = None, espsecure: Path | None = None) -> str:
    espsecure = espsecure or find_espsecure(platformio_env)
    existing_candidates = [path for path in candidate_python_executables(platformio_env) if path.exists()]
    probes = [probe_python_for_espsecure(candidate, espsecure) for candidate in existing_candidates]
    for probe in probes:
        if probe.can_run_espsecure:
            return str(probe.python)

    if not probes:
        raise BuildSecurityError("could not find any Python interpreters to run espsecure.py")

    best_probe = min(probes, key=lambda probe: len(probe.missing_modules))
    if best_probe.missing_modules:
        missing_packages = [
            ESPSECURE_PIP_PACKAGES.get(module, module) for module in best_probe.missing_modules
        ]
        raise BuildSecurityError(
            "could not find a Python interpreter with the packages needed by espsecure.py\n"
            f"Install them with: {best_probe.python} -m pip install {' '.join(missing_packages)}"
        )

    import_error = best_probe.import_error or "espsecure import failed without stderr output"
    raise BuildSecurityError(
        "found Python packages for espsecure.py, but espsecure itself still failed to import\n"
        f"Python: {best_probe.python}\n"
        f"espsecure.py: {espsecure}\n"
        f"Reproduce with: {best_probe.python} -c \"import sys; "
        f"sys.path.insert(0, {str(espsecure.parent)!r}); import espsecure\"\n"
        f"{import_error}"
    )


def run_command(args: list[str], *, cwd: Path = ROOT) -> None:
    print("running:", " ".join(str(arg) for arg in args))
    subprocess.run(args, cwd=cwd, check=True)


def files_match(left: Path, right: Path) -> bool:
    return left.exists() and right.exists() and filecmp.cmp(left, right, shallow=False)


def sign_binary(
    source: Path,
    *,
    signed: Path,
    unsigned: Path,
    key: Path,
    espsecure: Path,
    python: str,
) -> None:
    if not source.exists():
        raise BuildSecurityError(f"missing binary to sign: {source}")

    signing_source = source
    if files_match(source, signed) and unsigned.exists():
        signing_source = unsigned
    else:
        shutil.copy2(source, unsigned)

    if signed.exists():
        signed.unlink()

    run_command(
        [
            python,
            str(espsecure),
            "sign_data",
            "--version",
            "2",
            "--keyfile",
            str(key),
            "-o",
            str(signed),
            str(signing_source),
        ]
    )

    shutil.copy2(signed, source)
    print(f"signed {source.relative_to(ROOT)}")


def sign_secure_build_outputs(
    pioenv: str,
    *,
    platformio_env: Any | None = None,
) -> None:
    _, key_path = validate_secure_sdkconfig(pioenv)
    build_dir = ROOT / ".pio" / "build" / pioenv
    espsecure = find_espsecure(platformio_env)
    python = python_executable(platformio_env, espsecure)

    app_bin = build_dir / "firmware.bin"
    sign_binary(
        app_bin,
        signed=build_dir / "firmware-signed.bin",
        unsigned=build_dir / "firmware-unsigned.bin",
        key=key_path,
        espsecure=espsecure,
        python=python,
    )

    optional_outputs = (
        ("bootloader.bin", "bootloader-signed.bin", "bootloader-unsigned.bin"),
        (
            os.path.join("bootloader", "bootloader.bin"),
            os.path.join("bootloader", "bootloader-signed.bin"),
            os.path.join("bootloader", "bootloader-unsigned.bin"),
        ),
    )
    for source_name, signed_name, unsigned_name in optional_outputs:
        source = build_dir / source_name
        if source.exists():
            sign_binary(
                source,
                signed=build_dir / signed_name,
                unsigned=build_dir / unsigned_name,
                key=key_path,
                espsecure=espsecure,
                python=python,
            )


def build_dir_for_env(pioenv: str) -> Path:
    return ROOT / ".pio" / "build" / pioenv


def require_existing_file(path: Path, label: str) -> Path:
    if not path.exists():
        raise BuildSecurityError(f"missing {label}: {path}")
    return path


def validate_signed_flash_artifacts(pioenv: str) -> Path:
    validate_secure_sdkconfig(pioenv)
    build_dir = build_dir_for_env(pioenv)

    firmware = require_existing_file(build_dir / "firmware.bin", "signed firmware.bin")
    firmware_signed = require_existing_file(build_dir / "firmware-signed.bin", "firmware-signed.bin")
    require_existing_file(build_dir / "firmware-unsigned.bin", "firmware-unsigned.bin")
    require_existing_file(build_dir / "partitions.bin", "partitions.bin")

    if not files_match(firmware, firmware_signed):
        raise BuildSecurityError(
            f"{firmware.relative_to(ROOT)} does not match firmware-signed.bin; rebuild with "
            f"python tools\\build_security.py -e {pioenv}"
        )

    bootloader = build_dir / "bootloader.bin"
    bootloader_signed = build_dir / "bootloader-signed.bin"
    if bootloader.exists() or bootloader_signed.exists():
        require_existing_file(bootloader, "signed bootloader.bin")
        require_existing_file(bootloader_signed, "bootloader-signed.bin")
        require_existing_file(build_dir / "bootloader-unsigned.bin", "bootloader-unsigned.bin")
        if not files_match(bootloader, bootloader_signed):
            raise BuildSecurityError(
                f"{bootloader.relative_to(ROOT)} does not match bootloader-signed.bin; rebuild with "
                f"python tools\\build_security.py -e {pioenv}"
            )

    return build_dir


def detect_usb_serial_port() -> str:
    completed = subprocess.run(
        ["pio", "device", "list", "--json-output"],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=True,
    )

    devices = json.loads(completed.stdout)
    if not devices:
        raise BuildSecurityError("no serial devices found")

    usb_devices: list[dict[str, object]] = []
    preferred: list[dict[str, object]] = []

    for device in devices:
        blob = " ".join(
            str(device.get(key, ""))
            for key in ("description", "hwid", "manufacturer", "product")
        ).lower()

        if "usb" in blob or "vid:" in blob or "pid:" in blob:
            usb_devices.append(device)

        if any(
            marker in blob
            for marker in (
                "cp210",
                "ch340",
                "ch910",
                "ftdi",
                "silicon labs",
                "usb serial",
                "usb jtag",
                "esp32",
                "espressif",
            )
        ):
            preferred.append(device)

    matches = preferred or usb_devices
    if not matches:
        raise BuildSecurityError("no USB-backed serial COM port found")

    if len(matches) > 1:
        print("Multiple USB serial devices found; using the first one:")
        for device in matches:
            print(f"  {device.get('port')} - {device.get('description', '')}")

    port = str(matches[0].get("port", ""))
    if not port:
        raise BuildSecurityError("serial device list did not include a port")
    return port


def flash_files_from_manifest(build_dir: Path) -> dict[str, Path]:
    manifest_path = build_dir / "flasher_args.json"
    manifest = json.loads(require_existing_file(manifest_path, "flasher_args.json").read_text())
    raw_flash_files = manifest.get("flash_files")
    if not isinstance(raw_flash_files, dict):
        raise BuildSecurityError(f"missing flash_files in {manifest_path}")

    flash_files: dict[str, Path] = {}
    for offset, raw_path in raw_flash_files.items():
        path = build_dir / str(raw_path)
        name = path.name.lower()
        parent = path.parent.name.lower()

        if name in {"the-fly.bin", "firmware.bin"}:
            path = build_dir / "firmware.bin"
        elif name == "bootloader.bin":
            path = build_dir / "bootloader.bin"
        elif name in {"partition-table.bin", "partitions.bin"} or parent == "partition_table":
            path = build_dir / "partitions.bin"

        flash_files[str(offset)] = require_existing_file(path, f"flash file at {offset}")

    return flash_files


def flash_secure_build_outputs(pioenv: str, *, port: str, baud: int) -> None:
    build_dir = validate_signed_flash_artifacts(pioenv)
    flash_files = flash_files_from_manifest(build_dir)
    manifest = json.loads((build_dir / "flasher_args.json").read_text())
    flash_settings = manifest.get("flash_settings", {})
    extra_args = manifest.get("extra_esptool_args", {})

    resolved_port = detect_usb_serial_port() if port == "auto" else port
    esptool = find_esptool()
    python = python_executable(espsecure=find_espsecure())

    command = [
        python,
        str(esptool),
        "--chip",
        str(extra_args.get("chip", "esp32")),
        "--port",
        resolved_port,
        "--baud",
        str(baud),
        "--before",
        str(extra_args.get("before", "default_reset")),
        "--after",
        "hard_reset",
        "write_flash",
        "-z",
        "--flash_mode",
        str(flash_settings.get("flash_mode", "dio")),
        "--flash_freq",
        str(flash_settings.get("flash_freq", "40m")),
        "--flash_size",
        str(flash_settings.get("flash_size", "keep")),
    ]

    for offset in sorted(flash_files, key=lambda value: int(value, 16)):
        command.extend((offset, str(flash_files[offset])))

    print(f"Flashing signed secure build {pioenv} to {resolved_port}")
    run_command(command)


def remove_build_dir() -> None:
    build_dir = ROOT / ".pio" / "build"
    if build_dir.exists():
        print(f"removing {build_dir}")
        shutil.rmtree(build_dir)
    else:
        print(f"skipping missing {build_dir}")


def remove_sdkconfig_files() -> None:
    for path in sorted(ROOT.glob("sdkconfig.*")):
        if path.is_file():
            print(f"removing {path}")
            path.unlink()


def create_signing_key(key_path: Path, *, force: bool) -> None:
    if key_path.exists() and not force:
        raise BuildSecurityError(
            f"signing key already exists: {key_path}\n"
            f"Use --force-key only if you intentionally want to replace it."
        )

    if key_path.exists():
        key_path.unlink()

    espsecure = find_espsecure()
    python = python_executable(espsecure=espsecure)
    key_path.parent.mkdir(parents=True, exist_ok=True)
    run_command(
        [
            python,
            str(espsecure),
            "generate_signing_key",
            "--version",
            "2",
            str(key_path),
        ]
    )
    print(f"created {key_path}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build and sign The Fly secure PlatformIO environments."
    )
    parser.add_argument(
        "-e",
        "--environment",
        choices=SECURE_ENVS,
        default="thefly_sec1",
        help="Secure PlatformIO environment to build. Defaults to thefly_sec1.",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Remove .pio/build and sdkconfig.* before building.",
    )
    parser.add_argument(
        "--flash",
        action="store_true",
        help="After building and signing, flash the signed artifacts directly without invoking PlatformIO upload.",
    )
    parser.add_argument(
        "--port",
        default="auto",
        help='Serial port for --flash, like COM7. Defaults to "auto".',
    )
    parser.add_argument(
        "--baud",
        type=int,
        default=DEFAULT_FLASH_BAUD,
        help=f"Baud rate for --flash. Defaults to {DEFAULT_FLASH_BAUD}.",
    )
    parser.add_argument(
        "--create-key",
        action="store_true",
        help="Create the Secure Boot v2 private signing key and exit.",
    )
    parser.add_argument(
        "--force-key",
        action="store_true",
        help="Allow --create-key to replace an existing signing key.",
    )
    parser.add_argument(
        "--key",
        type=Path,
        default=DEFAULT_KEY,
        help="Secure Boot signing key path. Defaults to secure_boot_signing_key.pem.",
    )
    return parser.parse_args()


def cli_main() -> int:
    args = parse_args()
    key_path = args.key if args.key.is_absolute() else ROOT / args.key

    if args.create_key:
        create_signing_key(key_path, force=args.force_key)
        return 0

    if not key_path.exists():
        raise BuildSecurityError(
            f"secure boot signing key is missing: {key_path}\n"
            f"Create it with: python tools\\build_security.py --create-key"
        )

    if args.clean:
        remove_build_dir()
        remove_sdkconfig_files()
        run_command(["pio", "project", "config"])

    run_command(["pio", "run", "-e", args.environment])
    if args.flash:
        flash_secure_build_outputs(args.environment, port=args.port, baud=args.baud)
    return 0


def register_platformio_hook(platformio_env: Any) -> None:
    pioenv = str(platformio_env.get("PIOENV", ""))
    if pioenv not in SECURE_ENVS:
        return

    try:
        from SCons.Script import COMMAND_LINE_TARGETS
    except Exception:
        COMMAND_LINE_TARGETS = []

    if COMMAND_LINE_TARGETS and set(COMMAND_LINE_TARGETS) <= {"clean"}:
        return

    try:
        custom_sdkconfig = str(platformio_env.GetProjectOption("custom_sdkconfig"))
        configured = parse_sdkconfig_text(custom_sdkconfig)
        key_path = signing_key_from_values(configured)
        if not key_path.exists():
            raise BuildSecurityError(
                f"secure boot signing key is missing: {key_path}\n"
                f"Create it with: python tools\\build_security.py --create-key"
            )
    except BuildSecurityError as exc:
        print(f"build_security.py: error: {exc}", file=sys.stderr)
        platformio_env.Exit(1)

    def post_build_action(source: Any, target: Any, env: Any) -> int:
        try:
            sign_secure_build_outputs(pioenv, platformio_env=env)
        except (BuildSecurityError, subprocess.CalledProcessError, OSError) as exc:
            print(f"build_security.py: error: {exc}", file=sys.stderr)
            return 1
        return 0

    firmware_target = os.path.join("$BUILD_DIR", "${PROGNAME}.bin")
    platformio_env.AddPostAction(
        firmware_target,
        platformio_env.VerboseAction(
            post_build_action,
            "Signing Secure Boot v2 build artifacts",
        ),
    )


if "Import" in globals():
    Import("env")  # type: ignore[name-defined]
    register_platformio_hook(env)  # type: ignore[name-defined]
elif __name__ == "__main__":
    try:
        raise SystemExit(cli_main())
    except BuildSecurityError as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
    except subprocess.CalledProcessError as exc:
        raise SystemExit(exc.returncode)
    except OSError as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
