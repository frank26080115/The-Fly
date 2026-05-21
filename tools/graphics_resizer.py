#!/usr/bin/env python3

import argparse
from pathlib import Path
from PIL import Image
import numpy as np


def output_name(input_path: Path, size: int) -> str:
    name = input_path.name

    if name.lower().endswith(".fw.png"):
        stem = name[:-7]  # remove ".fw.png"
    else:
        stem = input_path.stem

    return f"{stem}_{size}.png"


def process_png(input_path: Path, output_path: Path, size: int, margin: int = 2):
    img = Image.open(input_path).convert("RGBA")
    arr = np.array(img)

    alpha = arr[:, :, 3]
    mask = alpha > 0

    if not mask.any():
        print(f"Skipping fully transparent image: {input_path}")
        return

    ys, xs = np.nonzero(mask)

    min_x, max_x = xs.min(), xs.max()
    min_y, max_y = ys.min(), ys.max()

    blob_w = max_x - min_x + 1
    blob_h = max_y - min_y + 1

    center_x = xs.mean()
    center_y = ys.mean()

    available = size - margin * 2
    scale = min(available / blob_w, available / blob_h)

    new_w = max(1, round(img.width * scale))
    new_h = max(1, round(img.height * scale))

    resized = img.resize((new_w, new_h), Image.Resampling.LANCZOS)

    scaled_center_x = center_x * scale
    scaled_center_y = center_y * scale

    canvas_center = size / 2

    paste_x = round(canvas_center - scaled_center_x)
    paste_y = round(canvas_center - scaled_center_y)

    # Clamp so the visible blob keeps at least `margin` pixels from edges
    scaled_min_x = min_x * scale + paste_x
    scaled_max_x = (max_x + 1) * scale + paste_x
    scaled_min_y = min_y * scale + paste_y
    scaled_max_y = (max_y + 1) * scale + paste_y

    if scaled_min_x < margin:
        paste_x += round(margin - scaled_min_x)
    if scaled_max_x > size - margin:
        paste_x -= round(scaled_max_x - (size - margin))
    if scaled_min_y < margin:
        paste_y += round(margin - scaled_min_y)
    if scaled_max_y > size - margin:
        paste_y -= round(scaled_max_y - (size - margin))

    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 255))
    canvas.alpha_composite(resized, (paste_x, paste_y))

    output_path.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(output_path, "PNG", optimize=True, compress_level=9)


def main():
    parser = argparse.ArgumentParser(
        description="Resize PNG icons using alpha-mask center of mass."
    )

    parser.add_argument(
        "input_dir",
        nargs="?",
        default="../gfx-assets/sprite_base",
        help="Directory containing PNG files.",
    )

    parser.add_argument(
        "output_dir",
        nargs="?",
        default="../gfx-assets/sprites_resized",
        help="Directory to save processed PNG files.",
    )

    parser.add_argument(
        "-s",
        "--size",
        type=int,
        default=100,
        help="Output square size in pixels. Default: 100",
    )

    args = parser.parse_args()

    input_dir = Path(args.input_dir)
    output_dir = Path(args.output_dir)

    if args.size < 5:
        raise ValueError("Size must be at least 5 pixels.")

    for input_path in sorted(input_dir.glob("*.png")):
        out_path = output_dir / output_name(input_path, args.size)
        process_png(input_path, out_path, args.size)
        print(f"{input_path.name} -> {out_path.name}")


if __name__ == "__main__":
    main()
