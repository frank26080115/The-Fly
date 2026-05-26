# use this if we run into that iram0 overflow error during build linker step
# note: this typically takes several minutes, way longer than 2 minutes

from pathlib import Path
import argparse
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


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


def run_command(args: list[str]) -> None:
    print(f"running: {' '.join(args)}")
    subprocess.run(args, cwd=ROOT, check=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Clean PlatformIO build artifacts and rebuild one environment."
    )
    parser.add_argument(
        "-e",
        "--environment",
        default="thefly_sec0",
        help="PlatformIO environment to build. Defaults to thefly_sec0.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    remove_build_dir()
    remove_sdkconfig_files()
    run_command(["pio", "project", "config"])
    run_command(["pio", "run", "-e", args.environment])
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as exc:
        raise SystemExit(exc.returncode)
    except OSError as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
