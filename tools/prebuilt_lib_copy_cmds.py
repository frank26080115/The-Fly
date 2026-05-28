#!/usr/bin/env python3

import argparse
from pathlib import Path
import os


def collect_a_files(root: Path):
    """
    Recursively collect all .a files.
    Returns:
        dict[str, Path]
        key = filename only
        value = full path
    """
    files = {}

    for path in root.rglob("*.a"):
        if path.is_file():
            files[path.name] = path

    return files


def main():
    parser = argparse.ArgumentParser(
        description="Generate copy commands for matching .a files."
    )

    parser.add_argument(
        "input_dir",
        help="Input directory to scan recursively"
    )

    parser.add_argument(
        "output_dir",
        help="Output directory to scan recursively"
    )

    args = parser.parse_args()

    input_dir = Path(args.input_dir).resolve()
    output_dir = Path(args.output_dir).resolve()

    if not input_dir.is_dir():
        raise SystemExit(f"Input directory does not exist: {input_dir}")

    if not output_dir.is_dir():
        raise SystemExit(f"Output directory does not exist: {output_dir}")

    input_files = collect_a_files(input_dir)
    output_files = collect_a_files(output_dir)

    matched = 0

    for name, input_path in sorted(input_files.items()):
        output_path = output_files.get(name)

        if output_path:
            matched += 1

            # Windows copy command
            print(
                f'copy /Y "{input_path}" "{output_path}"'
            )

    print(
        f"\nREM Matched {matched} file(s)",
        file=os.sys.stderr
    )


if __name__ == "__main__":
    main()
