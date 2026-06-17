#!/usr/bin/env python3
from __future__ import annotations

import colorsys
import math
from pathlib import Path

from PIL import Image


TILE_SIZE = 100
MAX_OUTPUT_SIZE = 2048
HUE_BUCKETS = 36
PALETTE_COLUMNS = 4
PALETTE_ROWS = 4
PALETTE_COLOR_COUNT = PALETTE_COLUMNS * PALETTE_ROWS
MIN_PALETTE_ALPHA = 32
MIN_PALETTE_SATURATION = 36
MIN_PALETTE_VALUE = 24
IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".webp", ".bmp", ".gif", ".tif", ".tiff"}

ROOT_DIR = Path(__file__).resolve().parents[1]
INPUT_DIR = ROOT_DIR / "gfx-assets" / "sprite_base"
OUTPUT_PATH = INPUT_DIR / "artboard.png"


def image_files(input_dir: Path, output_path: Path) -> list[Path]:
    return [
        path
        for path in sorted(input_dir.iterdir(), key=lambda item: item.name.lower())
        if path.is_file()
        and path.suffix.lower() in IMAGE_SUFFIXES
        and path.resolve() != output_path.resolve()
    ]


def normalize_image(input_path: Path, size: int = TILE_SIZE) -> Image.Image:
    with Image.open(input_path) as source:
        image = source.convert("RGBA")

    scale = min(size / image.width, size / image.height)
    resized_size = (
        max(1, round(image.width * scale)),
        max(1, round(image.height * scale)),
    )
    resized = image.resize(resized_size, Image.Resampling.LANCZOS)

    tile = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    paste_at = ((size - resized.width) // 2, (size - resized.height) // 2)
    tile.alpha_composite(resized, paste_at)
    return tile


def build_square_artboard(tiles: list[Image.Image]) -> Image.Image:
    grid_size = math.ceil(math.sqrt(len(tiles)))
    artboard_size = grid_size * TILE_SIZE
    artboard = Image.new("RGBA", (artboard_size, artboard_size), (0, 0, 0, 0))

    for index, tile in enumerate(tiles):
        row, column = divmod(index, grid_size)
        artboard.alpha_composite(tile, (column * TILE_SIZE, row * TILE_SIZE))

    return artboard


def hue_bucket(hue: int) -> int:
    return round(hue * HUE_BUCKETS / 256) % HUE_BUCKETS


def palette_pixel_weight(alpha: int, saturation: int, value: int) -> int:
    if alpha < MIN_PALETTE_ALPHA:
        return 0
    if saturation < MIN_PALETTE_SATURATION:
        return 0
    if value < MIN_PALETTE_VALUE:
        return 0

    return alpha * saturation * saturation * value


def top_hue_colors(image: Image.Image) -> list[tuple[int, int, int, int]]:
    rgba = image.convert("RGBA")
    hsv = rgba.convert("RGB").convert("HSV")
    buckets = [
        {"score": 0, "weight": 0, "s": 0, "v": 0}
        for _ in range(HUE_BUCKETS)
    ]

    rgba_bytes = rgba.tobytes()
    hsv_bytes = hsv.tobytes()

    for rgba_index in range(0, len(rgba_bytes), 4):
        hsv_index = (rgba_index // 4) * 3
        alpha = rgba_bytes[rgba_index + 3]
        hue = hsv_bytes[hsv_index]
        saturation = hsv_bytes[hsv_index + 1]
        value = hsv_bytes[hsv_index + 2]

        weight = palette_pixel_weight(alpha, saturation, value)
        if weight == 0:
            continue

        bucket = buckets[hue_bucket(hue)]
        bucket["score"] += weight
        bucket["weight"] += weight
        bucket["s"] += saturation * weight
        bucket["v"] += value * weight

    ranked = sorted(
        range(HUE_BUCKETS),
        key=lambda index: (buckets[index]["score"], -index),
        reverse=True,
    )
    selected = sorted(
        [index for index in ranked if buckets[index]["weight"] > 0][:PALETTE_COLOR_COUNT]
    )

    colors: list[tuple[int, int, int, int]] = []
    for index in selected:
        bucket = buckets[index]
        weight = bucket["weight"]
        hue = index / HUE_BUCKETS
        saturation = (bucket["s"] / weight) / 255
        value = (bucket["v"] / weight) / 255
        r, g, b = colorsys.hsv_to_rgb(hue, saturation, value)
        colors.append((round(r * 255), round(g * 255), round(b * 255), 255))

    while len(colors) < PALETTE_COLOR_COUNT:
        colors.append((0, 0, 0, 0))

    return colors


def palette_tile(colors: list[tuple[int, int, int, int]], size: int = TILE_SIZE) -> Image.Image:
    tile = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    swatch_width = size // PALETTE_COLUMNS
    swatch_height = size // PALETTE_ROWS

    for index, color in enumerate(colors[:PALETTE_COLOR_COUNT]):
        row, column = divmod(index, PALETTE_COLUMNS)
        left = column * swatch_width
        upper = row * swatch_height
        right = size if column == PALETTE_COLUMNS - 1 else left + swatch_width
        lower = size if row == PALETTE_ROWS - 1 else upper + swatch_height

        swatch = Image.new("RGBA", (right - left, lower - upper), color)
        tile.alpha_composite(swatch, (left, upper))

    return tile


def shrink_if_needed(image: Image.Image) -> Image.Image:
    if image.width <= MAX_OUTPUT_SIZE and image.height <= MAX_OUTPUT_SIZE:
        return image

    return image.resize((MAX_OUTPUT_SIZE, MAX_OUTPUT_SIZE), Image.Resampling.LANCZOS)


def generate_artboard(input_dir: Path = INPUT_DIR, output_path: Path = OUTPUT_PATH) -> Path:
    files = image_files(input_dir, output_path)
    if not files:
        raise ValueError(f"no image files found in {input_dir}")

    tiles = [normalize_image(input_path) for input_path in files]
    sprite_artboard = build_square_artboard(tiles)
    tiles.append(palette_tile(top_hue_colors(sprite_artboard)))

    artboard = shrink_if_needed(build_square_artboard(tiles))

    output_path.parent.mkdir(parents=True, exist_ok=True)
    artboard.save(output_path, "PNG", optimize=True, compress_level=9)
    return output_path


def main() -> int:
    output_path = generate_artboard()
    print(f"wrote {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
