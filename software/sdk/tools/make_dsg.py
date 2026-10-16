"""Package a linked DualSlide game (ELF) as a .dsg game file.

The layout is defined in firmware/system/inc/game_file.h; this script writes
format 1.0. Only the Python standard library is needed.

    python make_dsg.py --elf pong.elf --out pong.dsg --api-header game_api.h ^
        --id dualslide.pong --name Pong --version 1.0.0 ^
        --author "Jack Herron" --description "Classic bat and ball."

The ELF must be linked at address zero with --emit-relocs (sdk/game.ld does
this). Every absolute 32-bit address in the image becomes a relocation the
firmware applies after loading; PC-relative references need nothing. Any
other kind of relocation is refused, as the firmware could not apply it.
"""

import argparse
import re
import struct
import sys
import zlib

# --------------------------------------------------------------------------
# game_file.h
# --------------------------------------------------------------------------

SIGNATURE = bytes([0x89, ord("D"), ord("S"), ord("G"), ord("\r"), ord("\n"), 0x1A, ord("\n")])
FORMAT_MAJOR = 1
FORMAT_MINOR = 0
ARCHITECTURE_CORTEX_M7_HARD_FLOAT = 0x4648374D  # "M7HF"

CHUNK_INFO = 0x4F464E49         # "INFO"
CHUNK_CODE = 0x45444F43         # "CODE"
CHUNK_RELOCATIONS = 0x4F4C4552  # "RELO"
CHUNK_REQUIRED = 0x00000001

RELOCATION_ABSOLUTE_32 = 0
RELOCATION_OFFSET_MASK = 0x0FFFFFFF

HEADER_FORMAT = "<8sHHIIIIHH4H4HIII3I"  # GameFile_HeaderTypeDef, 72 bytes
CHUNK_FORMAT = "<IIII"                  # GameFile_ChunkTypeDef
CODE_FORMAT = "<IIII"                   # GameFile_CodeTypeDef

IDENTIFIER_SIZE = 48
NAME_SIZE = 32
AUTHOR_SIZE = 32
DESCRIPTION_SIZE = 128

SUMMARY_BYTES = 1024
ENTRY_SYMBOL = "DualSlideGame_Entry"

assert struct.calcsize(HEADER_FORMAT) == 72

# --------------------------------------------------------------------------
# ELF
# --------------------------------------------------------------------------

SHT_SYMTAB = 2
SHT_NOBITS = 8
SHT_REL = 9
SHT_RELA = 4
SHF_ALLOC = 0x2
SHN_UNDEF = 0
SHN_ABS = 0xFFF1
EM_ARM = 40

R_ARM_ABS32 = 2
R_ARM_TARGET1 = 38

# References relative to the code itself: correct wherever the game is loaded.
POSITION_INDEPENDENT_RELOCATIONS = {
    0,    # R_ARM_NONE
    3,    # R_ARM_REL32
    10,   # R_ARM_THM_CALL
    11,   # R_ARM_THM_PC8
    28,   # R_ARM_CALL
    29,   # R_ARM_JUMP24
    30,   # R_ARM_THM_JUMP24
    40,   # R_ARM_V4BX
    41,   # R_ARM_TARGET2 (relative on bare metal)
    42,   # R_ARM_PREL31
    51,   # R_ARM_THM_JUMP19
    53,   # R_ARM_THM_ALU_PREL_11_0
    54,   # R_ARM_THM_PC12
    102,  # R_ARM_THM_JUMP11
    103,  # R_ARM_THM_JUMP8
}


class Section:
    def __init__(self, name, type_, flags, address, offset, size, link, info, alignment, data):
        self.name = name
        self.type = type_
        self.flags = flags
        self.address = address
        self.offset = offset
        self.size = size
        self.link = link
        self.info = info
        self.alignment = max(alignment, 1)
        self.data = data


def fail(message):
    sys.exit(f"make_dsg: {message}")


def read_elf(path):
    with open(path, "rb") as file:
        elf = file.read()

    if elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] != 1:
        fail(f"{path} is not a 32-bit little-endian ELF file")

    machine, = struct.unpack_from("<H", elf, 18)
    if machine != EM_ARM:
        fail(f"{path} is not ARM code")

    section_offset, = struct.unpack_from("<I", elf, 32)
    section_entry_size, section_count, names_index = struct.unpack_from("<HHH", elf, 46)

    raw = []
    for index in range(section_count):
        raw.append(struct.unpack_from("<IIIIIIIIII", elf, section_offset + index * section_entry_size))

    names = raw[names_index]
    sections = []
    for name_offset, type_, flags, address, offset, size, link, info, alignment, _ in raw:
        start = names[4] + name_offset
        name = elf[start:elf.index(b"\0", start)].decode("ascii")
        data = b"" if type_ == SHT_NOBITS else elf[offset:offset + size]
        sections.append(Section(name, type_, flags, address, offset, size, link, info, alignment, data))

    return sections


def read_symbols(sections):
    symbol_table = next((section for section in sections if section.type == SHT_SYMTAB), None)
    if symbol_table is None:
        fail("the ELF has no symbol table; do not strip it")

    strings = sections[symbol_table.link].data
    symbols = []
    for offset in range(0, len(symbol_table.data), 16):
        name_offset, value, size, info, other, section_index = struct.unpack_from("<IIIBBH", symbol_table.data, offset)
        name = strings[name_offset:strings.index(b"\0", name_offset)].decode("ascii")
        symbols.append((name, value, section_index))

    return symbols

# --------------------------------------------------------------------------
# Image and relocations
# --------------------------------------------------------------------------


def build_image(sections):
    loaded = [s for s in sections if (s.flags & SHF_ALLOC) and s.type != SHT_NOBITS and s.size]
    zeroed = [s for s in sections if (s.flags & SHF_ALLOC) and s.type == SHT_NOBITS and s.size]

    image_size = max((s.address + s.size for s in loaded), default=0)
    image = bytearray(image_size)
    for section in loaded:
        image[section.address:section.address + section.size] = section.data

    for section in zeroed:
        if section.address < image_size:
            fail(f"zeroed section {section.name} lies inside the load image; check the linker script")

    memory_end = max([image_size] + [s.address + s.size for s in zeroed])
    alignment = max([8] + [s.alignment for s in sections if s.flags & SHF_ALLOC])

    return image, memory_end - image_size, alignment


def build_relocations(sections, symbols, image):
    relocations = []

    for section in sections:
        if section.type == SHT_RELA:
            fail(f"{section.name}: RELA relocations are not supported")

        if section.type != SHT_REL:
            continue

        target = sections[section.info]
        if not (target.flags & SHF_ALLOC) or target.type == SHT_NOBITS:
            continue  # Debug information and the like

        for offset in range(0, len(section.data), 8):
            address, info = struct.unpack_from("<II", section.data, offset)
            kind = info & 0xFF
            symbol_name, _, symbol_section = symbols[info >> 8]

            if kind in POSITION_INDEPENDENT_RELOCATIONS:
                continue

            if kind not in (R_ARM_ABS32, R_ARM_TARGET1):
                fail(f"relocation type {kind} against '{symbol_name}' in {target.name} cannot be loaded; "
                     "build with -mword-relocations")

            # Absolute symbols, and undefined weak symbols (NULL), do not move with the game.
            if symbol_section in (SHN_ABS, SHN_UNDEF):
                continue

            if address + 4 > len(image) or address > RELOCATION_OFFSET_MASK:
                fail(f"relocation at 0x{address:x} lies outside the load image")

            relocations.append(address)

    relocations.sort()
    return relocations

# --------------------------------------------------------------------------
# Game file
# --------------------------------------------------------------------------


def parse_version(text, what):
    match = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", text)
    if not match:
        fail(f"{what} must look like 1.2.3, not '{text}'")
    numbers = tuple(int(part) for part in match.groups())
    if any(number > 0xFFFF for number in numbers):
        fail(f"{what} numbers must be below 65536")
    return numbers


def read_api_version(path):
    with open(path, encoding="utf-8") as file:
        text = file.read()

    def define(name):
        match = re.search(rf"#define\s+{name}\s+\((\d+)U\)", text)
        if not match:
            fail(f"{name} not found in {path}")
        return int(match.group(1))

    api = (define("GAME_API_VERSION_MAJOR"), define("GAME_API_VERSION_MINOR"))
    firmware = (define("GAME_API_MINIMUM_FIRMWARE_MAJOR"),
                define("GAME_API_MINIMUM_FIRMWARE_MINOR"),
                define("GAME_API_MINIMUM_FIRMWARE_PATCH"))
    return api, firmware


def text_field(text, size, what):
    encoded = text.encode("utf-8")
    if len(encoded) >= size:
        fail(f"{what} is {len(encoded)} bytes; at most {size - 1} fit")
    return encoded.ljust(size, b"\0")


def align(value, alignment):
    return (value + alignment - 1) // alignment * alignment


def build_game_file(args):
    if not re.fullmatch(r"[a-z0-9]+(?:[.-][a-z0-9]+)+", args.id):
        fail(f"identifier '{args.id}' must be lower case words joined by dots or dashes, such as dualslide.pong")

    sections = read_elf(args.elf)
    symbols = read_symbols(sections)
    image, zero_size, alignment = build_image(sections)
    relocations = build_relocations(sections, symbols, image)

    entry = next((value for name, value, _ in symbols if name == ENTRY_SYMBOL), None)
    if entry is None or (entry & ~1) >= len(image):
        fail(f"{ENTRY_SYMBOL} is missing from the image")

    (api_major, api_minor), minimum_firmware = read_api_version(args.api_header)
    game_version = parse_version(args.version, "--version")

    info = (text_field(args.id, IDENTIFIER_SIZE, "--id")
            + text_field(args.name, NAME_SIZE, "--name")
            + text_field(args.author, AUTHOR_SIZE, "--author")
            + text_field(args.description, DESCRIPTION_SIZE, "--description"))
    code = struct.pack(CODE_FORMAT, len(image), zero_size, entry, alignment) + bytes(image)
    relocation_data = b"".join(struct.pack("<I", (RELOCATION_ABSOLUTE_32 << 28) | offset) for offset in relocations)

    header_size = struct.calcsize(HEADER_FORMAT)
    chunk_table_offset = header_size
    info_offset = chunk_table_offset + 3 * struct.calcsize(CHUNK_FORMAT)
    code_offset = align(info_offset + len(info), 8)
    relocation_offset = align(code_offset + len(code), 4)
    file_size = relocation_offset + len(relocation_data)

    if info_offset + len(info) > SUMMARY_BYTES:
        fail("the header and INFO chunk must fit in the first 1 KB")

    chunks = b"".join([
        struct.pack(CHUNK_FORMAT, CHUNK_INFO, CHUNK_REQUIRED, info_offset, len(info)),
        struct.pack(CHUNK_FORMAT, CHUNK_CODE, CHUNK_REQUIRED, code_offset, len(code)),
        struct.pack(CHUNK_FORMAT, CHUNK_RELOCATIONS, CHUNK_REQUIRED, relocation_offset, len(relocation_data)),
    ])

    body = bytearray(file_size)
    body[chunk_table_offset:chunk_table_offset + len(chunks)] = chunks
    body[info_offset:info_offset + len(info)] = info
    body[code_offset:code_offset + len(code)] = code
    body[relocation_offset:relocation_offset + len(relocation_data)] = relocation_data

    memory_size = len(image) + zero_size

    def header(crc):
        return struct.pack(HEADER_FORMAT,
                           SIGNATURE, FORMAT_MAJOR, FORMAT_MINOR, header_size, file_size, crc,
                           ARCHITECTURE_CORTEX_M7_HARD_FLOAT, api_major, api_minor,
                           *minimum_firmware, 0,
                           *game_version, 0,
                           chunk_table_offset, 3, memory_size, 0, 0, 0)

    body[0:header_size] = header(0)
    crc = zlib.crc32(bytes(body[24:])) & 0xFFFFFFFF
    body[0:header_size] = header(crc)

    with open(args.out, "wb") as file:
        file.write(body)

    print(f"{args.out}: {args.name} {args.version}, {len(image)} bytes of image, {zero_size} zeroed, "
          f"{len(relocations)} relocations, {memory_size // 1024 + 1} KB in memory, API {api_major}.{api_minor}")


def main():
    parser = argparse.ArgumentParser(description="Package a linked DualSlide game as a .dsg game file.")
    parser.add_argument("--elf", required=True, help="linked game, from sdk/game.ld with --emit-relocs")
    parser.add_argument("--out", required=True, help="game file to write")
    parser.add_argument("--api-header", required=True, help="game_api.h the game was built with")
    parser.add_argument("--id", required=True, help="unique, permanent identifier, such as dualslide.pong")
    parser.add_argument("--name", required=True, help="name shown in the launcher")
    parser.add_argument("--version", required=True, help="game version, such as 1.0.0")
    parser.add_argument("--author", default="", help="who made the game")
    parser.add_argument("--description", default="", help="one or two sentences about the game")
    build_game_file(parser.parse_args())


if __name__ == "__main__":
    main()
