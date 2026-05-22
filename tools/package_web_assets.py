"""
Package the admin web UI into a gzip-compressed firmware header.

PlatformIO loads this file as a pre-build script using the default project
paths. It can also be called directly when a different input/output pair or
packaging mode is useful.
"""

from __future__ import annotations

import argparse
import gzip
import mimetypes
import re
from dataclasses import dataclass
from io import BytesIO
from pathlib import Path
from typing import Iterable

try:
    import htmlmin
except ImportError:
    htmlmin = None

try:
    import csscompressor
except ImportError:
    csscompressor = None

try:
    from jsmin import jsmin
except ImportError:
    jsmin = None

try:
    from PIL import Image
except ImportError:
    Image = None


WEBASSET_FILENAME_LEN = 64
WEBASSET_MIMETYPE_LEN = 64
TEXT_ENCODINGS = ("utf-8",)

MIME_TYPES = {
    ".css": "text/css",
    ".html": "text/html",
    ".htm": "text/html",
    ".js": "text/javascript",
}

HTML_LINK_RE = re.compile(r"<link\b[^>]*>", re.IGNORECASE)
HTML_SCRIPT_RE = re.compile(r"<script\b[^>]*>.*?</script\s*>", re.IGNORECASE | re.DOTALL)
HTML_ATTR_RE = re.compile(
    r"""(?P<name>[A-Za-z_:][-A-Za-z0-9_:.]*)\s*=\s*(?:
        "(?P<double>[^"]*)" |
        '(?P<single>[^']*)' |
        (?P<bare>[^\s"'=<>`]+)
    )""",
    re.VERBOSE,
)
IDENTIFIER_RE = re.compile(r"[^A-Za-z0-9]+")


@dataclass(frozen=True)
class Asset:
    file_name: str
    mime_type: str
    macro_name: str
    array_name: str
    raw_payload: bytes
    gzip_payload: bytes


def project_root() -> Path:
    return Path(__file__).resolve().parents[1]


def build_parser(default_root: Path) -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Package web assets into a gzip-compressed C header.")
    parser.add_argument(
        "--input-directory",
        default=str(default_root / "html"),
        help="Directory to scan for web assets. Defaults to this project's html directory.",
    )
    parser.add_argument(
        "--output-header-file",
        default=str(default_root / "inc" / "web_assets.h"),
        help="Header file to generate. Defaults to inc/web_assets.h.",
    )
    parser.add_argument(
        "--minify",
        action="store_true",
        help="Minify HTML, CSS, and JavaScript before gzip compression.",
    )
    parser.add_argument(
        "--include-css",
        action="store_true",
        help="Include CSS files as standalone assets.",
    )
    parser.add_argument(
        "--include-js",
        action="store_true",
        help="Include JavaScript files as standalone assets.",
    )
    parser.add_argument(
        "--html-sub",
        action="store_true",
        help="Inline local stylesheet links and script src tags inside HTML assets.",
    )
    return parser


def require_minifier(value: object, package_name: str, file_kind: str) -> None:
    if value is None:
        raise RuntimeError(
            f"--minify needs the {package_name!r} Python package to minify {file_kind} files."
        )


def minify_css_text(text: str, enabled: bool) -> str:
    if not enabled:
        return text
    require_minifier(csscompressor, "csscompressor", "CSS")
    return csscompressor.compress(text)


def minify_html_text(text: str, enabled: bool) -> str:
    if not enabled:
        return text
    require_minifier(htmlmin, "htmlmin", "HTML")
    return htmlmin.minify(text, remove_comments=True, remove_empty_space=True)


def minify_js_text(text: str, enabled: bool) -> str:
    if not enabled:
        return text
    require_minifier(jsmin, "jsmin", "JavaScript")
    return jsmin(text)


def optimize_png_payload(path: Path) -> bytes:
    if Image is None:
        raise RuntimeError("PNG assets need the 'Pillow' Python package for lossless optimization.")

    output = BytesIO()
    try:
        with Image.open(path) as image:
            image.load()
            if getattr(image, "is_animated", False):
                raise RuntimeError(f"Animated PNG assets are not supported: {path}")

            save_options: dict[str, object] = {
                "format": "PNG",
                "optimize": True,
            }
            if "transparency" in image.info:
                save_options["transparency"] = image.info["transparency"]

            # Omit pnginfo, ICC, EXIF, and text chunks while re-encoding.
            image.save(output, **save_options)
    except OSError as error:
        raise RuntimeError(f"Could not optimize PNG asset {path}: {error}") from error
    return output.getvalue()


def read_text(path: Path) -> str:
    last_error: UnicodeDecodeError | None = None
    for encoding in TEXT_ENCODINGS:
        try:
            return path.read_text(encoding=encoding)
        except UnicodeDecodeError as error:
            last_error = error
    raise RuntimeError(f"Could not decode text asset {path}: {last_error}")


def html_attrs(tag: str) -> dict[str, str]:
    attrs: dict[str, str] = {}
    for match in HTML_ATTR_RE.finditer(tag):
        value = match.group("double")
        if value is None:
            value = match.group("single")
        if value is None:
            value = match.group("bare")
        attrs[match.group("name").lower()] = value or ""
    return attrs


def local_reference_path(root: Path, html_path: Path, reference: str) -> Path | None:
    reference = reference.strip()
    lower_reference = reference.lower()
    if not reference or lower_reference.startswith(("http:", "https:", "//", "data:", "#")):
        return None

    reference_path = reference.split("#", 1)[0].split("?", 1)[0]
    if not reference_path:
        return None

    if reference_path.startswith("/"):
        candidate = root / reference_path.lstrip("/")
    else:
        candidate = html_path.parent / reference_path

    resolved_root = root.resolve()
    resolved_candidate = candidate.resolve()
    try:
        resolved_candidate.relative_to(resolved_root)
    except ValueError as error:
        raise RuntimeError(f"HTML reference escapes input directory: {reference}") from error

    if not resolved_candidate.is_file():
        raise RuntimeError(f"HTML reference not found: {reference} from {html_path}")
    return resolved_candidate


def inline_html_references(root: Path, html_path: Path, html_text: str, minify: bool) -> str:
    def replace_stylesheet(match: re.Match[str]) -> str:
        tag = match.group(0)
        attrs = html_attrs(tag)
        rel_values = attrs.get("rel", "").lower().split()
        css_path = local_reference_path(root, html_path, attrs.get("href", ""))
        if "stylesheet" not in rel_values or css_path is None:
            return tag
        css_text = minify_css_text(read_text(css_path), minify)
        return f"<style>\n{css_text}\n</style>"

    def replace_script(match: re.Match[str]) -> str:
        tag = match.group(0)
        attrs = html_attrs(tag)
        js_path = local_reference_path(root, html_path, attrs.get("src", ""))
        if js_path is None:
            return tag
        js_text = minify_js_text(read_text(js_path), minify)
        return f"<script>\n{js_text}\n</script>"

    html_text = HTML_LINK_RE.sub(replace_stylesheet, html_text)
    return HTML_SCRIPT_RE.sub(replace_script, html_text)


def mime_type_for(path: Path) -> str:
    suffix = path.suffix.lower()
    if suffix in MIME_TYPES:
        return MIME_TYPES[suffix]
    guessed_type, _ = mimetypes.guess_type(path.name)
    return guessed_type or "application/octet-stream"


def checked_c_string(text: str, max_size: int, field_name: str) -> str:
    if len(text.encode("utf-8")) >= max_size:
        raise RuntimeError(f"{field_name} is too long for web_asset_desc_t: {text}")
    return text.replace("\\", "\\\\").replace('"', '\\"')


def asset_identifier(relative_name: str, used_identifiers: set[str]) -> str:
    identifier = IDENTIFIER_RE.sub("_", relative_name).strip("_").lower()
    identifier = re.sub(r"_+", "_", identifier)
    if not identifier:
        identifier = "asset"
    if identifier[0].isdigit():
        identifier = f"asset_{identifier}"

    unique_identifier = identifier
    suffix = 2
    while unique_identifier in used_identifiers:
        unique_identifier = f"{identifier}_{suffix}"
        suffix += 1
    used_identifiers.add(unique_identifier)
    return unique_identifier


def should_package(path: Path, include_css: bool, include_js: bool) -> bool:
    suffix = path.suffix.lower()
    if suffix == ".css":
        return include_css
    if suffix == ".js":
        return include_js
    return True


def processed_payload(root: Path, path: Path, minify: bool, html_sub: bool) -> bytes:
    suffix = path.suffix.lower()
    if suffix in (".html", ".htm"):
        text = read_text(path)
        if html_sub:
            text = inline_html_references(root, path, text, minify)
        text = minify_html_text(text, minify)
        return text.encode("utf-8")
    if suffix == ".css":
        return minify_css_text(read_text(path), minify).encode("utf-8")
    if suffix == ".js":
        return minify_js_text(read_text(path), minify).encode("utf-8")
    if suffix == ".png":
        return optimize_png_payload(path)
    return path.read_bytes()


def collect_assets(
    input_directory: Path,
    minify: bool,
    include_css: bool,
    include_js: bool,
    html_sub: bool,
) -> list[Asset]:
    if not input_directory.is_dir():
        raise RuntimeError(f"Input directory does not exist: {input_directory}")

    used_identifiers: set[str] = set()
    assets: list[Asset] = []
    for path in sorted(candidate for candidate in input_directory.rglob("*") if candidate.is_file()):
        if not should_package(path, include_css, include_js):
            continue

        relative_name = path.relative_to(input_directory).as_posix()
        identifier = asset_identifier(relative_name, used_identifiers)
        raw_payload = processed_payload(input_directory, path, minify, html_sub)
        assets.append(
            Asset(
                file_name=relative_name,
                mime_type=mime_type_for(path),
                macro_name=f"WEBASSET_{identifier.upper()}",
                array_name=f"webasset_{identifier}",
                raw_payload=raw_payload,
                gzip_payload=gzip.compress(raw_payload, compresslevel=9, mtime=0),
            )
        )
    return assets


def byte_lines(payload: bytes) -> Iterable[str]:
    for offset in range(0, len(payload), 16):
        chunk = payload[offset : offset + 16]
        yield "    " + ", ".join(f"0x{byte:02X}" for byte in chunk) + ","


def asset_array_lines(asset: Asset) -> list[str]:
    lines = [
        f"#define {asset.macro_name}_CSIZE {len(asset.gzip_payload)}",
        f"#define {asset.macro_name}_USIZE {len(asset.raw_payload)}",
        f"static const uint8_t {asset.array_name}[{asset.macro_name}_CSIZE] = {{",
    ]
    lines.extend(byte_lines(asset.gzip_payload))
    lines.extend(("};", ""))
    return lines


def header_text(assets: list[Asset]) -> str:
    lines = [
        "#pragma once",
        "",
        "/* Auto-generated by tools/package_web_assets.py. */",
        '#include "defs.h"',
        "",
    ]

    for asset in assets:
        lines.extend(asset_array_lines(asset))

    lines.extend(
        (
            f"#define WEB_ASSETS_CNT {len(assets)}",
            "static const web_asset_desc_t web_assets_list[WEB_ASSETS_CNT] = {",
        )
    )

    for asset in assets:
        file_name = checked_c_string(asset.file_name, WEBASSET_FILENAME_LEN, "file_name")
        mime_type = checked_c_string(asset.mime_type, WEBASSET_MIMETYPE_LEN, "mime_type")
        lines.extend(
            (
                "    {",
                f'        .file_name         = "{file_name}",',
                f'        .mime_type         = "{mime_type}",',
                f"        .uncompressed_size = {asset.macro_name}_USIZE,",
                f"        .compressed_size   = {asset.macro_name}_CSIZE,",
                f"        .ptr_payload       = {asset.array_name},",
                "    },",
            )
        )
    lines.extend(("};", ""))
    return "\n".join(lines)


def write_if_changed(output_header: Path, content: str) -> None:
    output_header.parent.mkdir(parents=True, exist_ok=True)
    if output_header.exists() and output_header.read_text(encoding="utf-8") == content:
        return
    output_header.write_text(content, encoding="utf-8", newline="\n")


def package_web_assets(
    input_directory: Path,
    output_header: Path,
    minify: bool = False,
    include_css: bool = False,
    include_js: bool = False,
    html_sub: bool = False,
) -> list[Asset]:
    assets = collect_assets(input_directory, minify, include_css, include_js, html_sub)
    write_if_changed(output_header, header_text(assets))
    print(f"Packaged {len(assets)} web assets from {input_directory} into {output_header}")
    return assets


def main(argv: list[str] | None = None) -> int:
    root = project_root()
    args = build_parser(root).parse_args(argv)
    package_web_assets(
        Path(args.input_directory),
        Path(args.output_header_file),
        args.minify,
        args.include_css,
        args.include_js,
        args.html_sub,
    )
    return 0


def package_platformio_defaults() -> None:
    Import("env")
    root = Path(env.subst("$PROJECT_DIR"))
    package_web_assets(root / "html", root / "inc" / "web_assets.h")


if "Import" in globals():
    package_platformio_defaults()
elif __name__ == "__main__":
    raise SystemExit(main())
