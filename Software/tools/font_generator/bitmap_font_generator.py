#!/usr/bin/env python3
"""
bitmap_font_generator.py

Batch-convert a TrueType/OpenType font into compact 1-bit C font assets.

Output:
    font.h
    <output_name>.h
    <output_name>.c

Requirements:
    python -m pip install pillow

Usage:
    1. Edit the CONFIGURATION section below.
    2. Run:
           python bitmap_font_generator.py

The generated glyph bitmaps:
    - are 1 bit per pixel;
    - are packed MSB-first;
    - begin on a byte boundary per glyph;
    - contain no LVGL dependencies;
    - cover one contiguous Unicode range.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import argparse
import re
import sys
from typing import Iterable, Sequence

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError as exc:
    raise SystemExit(
        "Pillow is required. Install it with:\n"
        "    python -m pip install pillow"
    ) from exc


# =============================================================================
# CONFIGURATION
# =============================================================================

FONT_FILE = Path("OpenSans-VariableFont_wdth,wght.ttf")
OUTPUT_DIRECTORY = Path("generated")
OUTPUT_NAME = "open_sans"
PUBLIC_NAME = "OpenSans"

FONT_SIZES = [12, 16, 20, 28, 36]

FIRST_CODEPOINT = 32
LAST_CODEPOINT = 126

# Pixels with coverage at or above this value become set in the 1-bit bitmap.
# 128 is a balanced default. Lower values make glyphs slightly heavier.
THRESHOLD = 128


# =============================================================================
# DATA TYPES
# =============================================================================

@dataclass(frozen=True)
class Glyph:
    codepoint: int
    bitmap_offset: int
    width: int
    height: int
    advance: int
    offset_x: int
    offset_y: int
    bitmap: bytes


@dataclass(frozen=True)
class GeneratedFont:
    size: int
    ascent: int
    descent: int
    line_height: int
    glyphs: tuple[Glyph, ...]
    bitmap: bytes


# =============================================================================
# VALIDATION AND NAMING
# =============================================================================

def validate_configuration(
    font_file: Path,
    sizes: Sequence[int],
    first_codepoint: int,
    last_codepoint: int,
    threshold: int,
) -> None:
    if not font_file.is_file():
        raise FileNotFoundError(f"Font file not found: {font_file}")

    if not sizes:
        raise ValueError("At least one font size must be specified.")

    if any(size <= 0 or size > 255 for size in sizes):
        raise ValueError("Font sizes must be between 1 and 255 pixels.")

    if len(set(sizes)) != len(sizes):
        raise ValueError("Font sizes must not contain duplicates.")

    if first_codepoint < 0 or last_codepoint > 0x10FFFF:
        raise ValueError("Codepoints must be valid Unicode values.")

    if first_codepoint > last_codepoint:
        raise ValueError("FIRST_CODEPOINT must not exceed LAST_CODEPOINT.")

    if not 0 <= threshold <= 255:
        raise ValueError("THRESHOLD must be between 0 and 255.")


def to_c_identifier(value: str) -> str:
    identifier = re.sub(r"[^A-Za-z0-9_]", "_", value)
    identifier = re.sub(r"_+", "_", identifier).strip("_")

    if not identifier:
        raise ValueError("Name cannot be converted into a valid C identifier.")

    if identifier[0].isdigit():
        identifier = "_" + identifier

    return identifier


def to_include_guard(value: str) -> str:
    return to_c_identifier(value).upper() + "_H"


# =============================================================================
# RASTERIZATION
# =============================================================================

def pack_bitmap(image: Image.Image, threshold: int) -> bytes:
    """
    Pack an 8-bit grayscale glyph image into a contiguous MSB-first bitstream.

    Each glyph begins at a byte boundary. Rows are not separately byte-aligned.
    """
    if image.width == 0 or image.height == 0:
        return b""

    pixels = image.load()
    output = bytearray()
    current_byte = 0
    bit_count = 0

    for y in range(image.height):
        for x in range(image.width):
            current_byte = (current_byte << 1) | int(pixels[x, y] >= threshold)
            bit_count += 1

            if bit_count == 8:
                output.append(current_byte)
                current_byte = 0
                bit_count = 0

    if bit_count:
        current_byte <<= 8 - bit_count
        output.append(current_byte)

    return bytes(output)


def rasterize_glyph(
    font: ImageFont.FreeTypeFont,
    codepoint: int,
    bitmap_offset: int,
    threshold: int,
) -> Glyph:
    character = chr(codepoint)

    # "ls" means left-baseline. The returned bounding box is relative to the
    # pen position at the baseline, which maps cleanly to embedded rendering.
    left, top, right, bottom = font.getbbox(character, anchor="ls")

    width = max(0, right - left)
    height = max(0, bottom - top)

    # Integer advances keep the runtime renderer simple.
    advance = max(0, int(round(font.getlength(character))))

    if width == 0 or height == 0:
        bitmap = b""
    else:
        image = Image.new("L", (width, height), 0)
        draw = ImageDraw.Draw(image)

        # Shift the glyph so its bounding box begins at image coordinate (0, 0).
        draw.text(
            (-left, -top),
            character,
            font=font,
            fill=255,
            anchor="ls",
        )

        bitmap = pack_bitmap(image, threshold)

    for field_name, value, minimum, maximum in (
        ("width", width, 0, 255),
        ("height", height, 0, 255),
        ("advance", advance, 0, 255),
        ("offset_x", left, -128, 127),
        ("offset_y", top, -128, 127),
    ):
        if not minimum <= value <= maximum:
            raise ValueError(
                f"Glyph U+{codepoint:04X} {field_name}={value} is outside "
                f"the supported range {minimum}..{maximum}."
            )

    return Glyph(
        codepoint=codepoint,
        bitmap_offset=bitmap_offset,
        width=width,
        height=height,
        advance=advance,
        offset_x=left,
        offset_y=top,
        bitmap=bitmap,
    )


def generate_font(
    font_file: Path,
    size: int,
    first_codepoint: int,
    last_codepoint: int,
    threshold: int,
) -> GeneratedFont:
    font = ImageFont.truetype(str(font_file), size=size)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent

    glyphs: list[Glyph] = []
    combined_bitmap = bytearray()

    for codepoint in range(first_codepoint, last_codepoint + 1):
        glyph = rasterize_glyph(
            font=font,
            codepoint=codepoint,
            bitmap_offset=len(combined_bitmap),
            threshold=threshold,
        )
        glyphs.append(glyph)
        combined_bitmap.extend(glyph.bitmap)

    return GeneratedFont(
        size=size,
        ascent=ascent,
        descent=descent,
        line_height=line_height,
        glyphs=tuple(glyphs),
        bitmap=bytes(combined_bitmap),
    )


# =============================================================================
# C OUTPUT
# =============================================================================

def format_byte_array(data: bytes, indent: str = "    ", columns: int = 12) -> str:
    if not data:
        return f"{indent}0x00"

    lines: list[str] = []

    for start in range(0, len(data), columns):
        chunk = data[start:start + columns]
        values = ", ".join(f"0x{value:02X}" for value in chunk)
        lines.append(f"{indent}{values},")

    return "\n".join(lines)


def character_comment(codepoint: int) -> str:
    character = chr(codepoint)

    escaped = {
        "\\": "\\\\",
        "\"": "\\\"",
        "\n": "\\n",
        "\r": "\\r",
        "\t": "\\t",
    }.get(character, character)

    if character.isprintable():
        return f'U+{codepoint:04X} "{escaped}"'

    return f"U+{codepoint:04X}"


def render_font_h() -> str:
    return """\
/**
 * @file font.h
 * @brief Generic packed 1-bit bitmap-font definitions.
 *
 * This file was generated by bitmap_font_generator.py.
 */

#ifndef FONT_H
#define FONT_H

#include <stdint.h>

/**
 * @brief Description of a single glyph.
 *
 * offsetX and offsetY are relative to the text pen's baseline position.
 * A glyph pixel at local coordinate (x, y) is drawn at:
 *
 *     screenX = penX + offsetX + x
 *     screenY = baselineY + offsetY + y
 */
typedef struct
{
    uint32_t bitmapOffset;

    uint8_t width;
    uint8_t height;
    uint8_t advance;

    int8_t offsetX;
    int8_t offsetY;
} FontGlyph;

/**
 * @brief Description of a packed 1-bit bitmap font.
 *
 * Each glyph begins at a byte boundary. Within a glyph, pixels are packed
 * left-to-right and top-to-bottom, most-significant bit first.
 */
typedef struct
{
    const uint8_t *bitmap;
    const FontGlyph *glyphs;

    uint32_t firstCodepoint;
    uint16_t glyphCount;

    uint8_t nominalSize;
    uint8_t lineHeight;
    uint8_t ascent;
    uint8_t descent;
} Font;

#endif /* FONT_H */
"""


def render_public_header(
    output_name: str,
    public_name: str,
    sizes: Sequence[int],
) -> str:
    guard = to_include_guard(output_name)
    declarations = "\n".join(
        f"extern const Font {public_name}{size};" for size in sizes
    )

    return f"""\
/**
 * @file {output_name}.h
 * @brief Generated {public_name} packed 1-bit bitmap fonts.
 *
 * This file was generated by bitmap_font_generator.py.
 */

#ifndef {guard}
#define {guard}

#include "font.h"

{declarations}

#endif /* {guard} */
"""


def render_glyph_table(font: GeneratedFont, table_name: str) -> str:
    entries: list[str] = []

    for glyph in font.glyphs:
        entries.append(
            f"""\
    /* {character_comment(glyph.codepoint)} */
    {{
        .bitmapOffset = {glyph.bitmap_offset}U,
        .width = {glyph.width}U,
        .height = {glyph.height}U,
        .advance = {glyph.advance}U,
        .offsetX = {glyph.offset_x},
        .offsetY = {glyph.offset_y}
    }}"""
        )

    return (
        f"static const FontGlyph {table_name}[] =\n"
        "{\n"
        + ",\n".join(entries)
        + "\n};"
    )


def render_font_definition(
    font: GeneratedFont,
    public_name: str,
    first_codepoint: int,
) -> str:
    bitmap_name = f"{public_name}{font.size}_Bitmap"
    glyphs_name = f"{public_name}{font.size}_Glyphs"
    object_name = f"{public_name}{font.size}"

    bitmap = (
        f"static const uint8_t {bitmap_name}[] =\n"
        "{\n"
        f"{format_byte_array(font.bitmap)}\n"
        "};"
    )

    glyphs = render_glyph_table(font, glyphs_name)

    descriptor = f"""\
const Font {object_name} =
{{
    .bitmap = {bitmap_name},
    .glyphs = {glyphs_name},

    .firstCodepoint = {first_codepoint}U,
    .glyphCount = {len(font.glyphs)}U,

    .nominalSize = {font.size}U,
    .lineHeight = {font.line_height}U,
    .ascent = {font.ascent}U,
    .descent = {font.descent}U
}};"""

    return f"""\
/* -------------------------------------------------------------------------- */
/* {public_name} {font.size} px */
/* Bitmap bytes: {len(font.bitmap)} */
/* -------------------------------------------------------------------------- */

{bitmap}

{glyphs}

{descriptor}
"""


def render_source(
    output_name: str,
    public_name: str,
    fonts: Sequence[GeneratedFont],
    first_codepoint: int,
    last_codepoint: int,
) -> str:
    definitions = "\n\n".join(
        render_font_definition(font, public_name, first_codepoint)
        for font in fonts
    )

    total_bitmap_bytes = sum(len(font.bitmap) for font in fonts)

    return f"""\
/**
 * @file {output_name}.c
 * @brief Generated {public_name} packed 1-bit bitmap-font data.
 *
 * Source range: U+{first_codepoint:04X} through U+{last_codepoint:04X}
 * Total bitmap bytes: {total_bitmap_bytes}
 *
 * This file was generated by bitmap_font_generator.py.
 */

#include "{output_name}.h"

{definitions}
"""


# =============================================================================
# FILE GENERATION
# =============================================================================

def write_text_file(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8", newline="\n")


def generate_files(
    font_file: Path,
    output_directory: Path,
    output_name: str,
    public_name: str,
    sizes: Sequence[int],
    first_codepoint: int,
    last_codepoint: int,
    threshold: int,
) -> None:
    validate_configuration(
        font_file=font_file,
        sizes=sizes,
        first_codepoint=first_codepoint,
        last_codepoint=last_codepoint,
        threshold=threshold,
    )

    output_name = to_c_identifier(output_name).lower()
    public_name = to_c_identifier(public_name)

    fonts: list[GeneratedFont] = []

    for size in sizes:
        print(f"Generating {public_name} {size} px...")
        fonts.append(
            generate_font(
                font_file=font_file,
                size=size,
                first_codepoint=first_codepoint,
                last_codepoint=last_codepoint,
                threshold=threshold,
            )
        )

    output_directory.mkdir(parents=True, exist_ok=True)

    write_text_file(output_directory / "font.h", render_font_h())
    write_text_file(
        output_directory / f"{output_name}.h",
        render_public_header(output_name, public_name, sizes),
    )
    write_text_file(
        output_directory / f"{output_name}.c",
        render_source(
            output_name=output_name,
            public_name=public_name,
            fonts=fonts,
            first_codepoint=first_codepoint,
            last_codepoint=last_codepoint,
        ),
    )

    total_bitmap_bytes = sum(len(font.bitmap) for font in fonts)

    print()
    print("Generated:")
    print(f"  {output_directory / 'font.h'}")
    print(f"  {output_directory / f'{output_name}.h'}")
    print(f"  {output_directory / f'{output_name}.c'}")
    print()
    print(f"Total bitmap data: {total_bitmap_bytes} bytes")


# =============================================================================
# COMMAND-LINE ENTRY POINT
# =============================================================================

def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate compact 1-bit C bitmap fonts from a TTF/OTF file."
    )

    parser.add_argument(
        "--font",
        type=Path,
        default=FONT_FILE,
        help=f"Input TTF/OTF file. Default: {FONT_FILE}",
    )
    parser.add_argument(
        "--output-directory",
        type=Path,
        default=OUTPUT_DIRECTORY,
        help=f"Output directory. Default: {OUTPUT_DIRECTORY}",
    )
    parser.add_argument(
        "--output-name",
        default=OUTPUT_NAME,
        help=f"Generated C/H filename stem. Default: {OUTPUT_NAME}",
    )
    parser.add_argument(
        "--public-name",
        default=PUBLIC_NAME,
        help=f"Public C object prefix. Default: {PUBLIC_NAME}",
    )
    parser.add_argument(
        "--sizes",
        type=int,
        nargs="+",
        default=FONT_SIZES,
        help="Font sizes in pixels.",
    )
    parser.add_argument(
        "--first-codepoint",
        type=int,
        default=FIRST_CODEPOINT,
        help=f"First Unicode codepoint. Default: {FIRST_CODEPOINT}",
    )
    parser.add_argument(
        "--last-codepoint",
        type=int,
        default=LAST_CODEPOINT,
        help=f"Last Unicode codepoint. Default: {LAST_CODEPOINT}",
    )
    parser.add_argument(
        "--threshold",
        type=int,
        default=THRESHOLD,
        help=f"1-bit rasterization threshold, 0..255. Default: {THRESHOLD}",
    )

    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()

    try:
        generate_files(
            font_file=arguments.font,
            output_directory=arguments.output_directory,
            output_name=arguments.output_name,
            public_name=arguments.public_name,
            sizes=arguments.sizes,
            first_codepoint=arguments.first_codepoint,
            last_codepoint=arguments.last_codepoint,
            threshold=arguments.threshold,
        )
    except (FileNotFoundError, OSError, ValueError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())