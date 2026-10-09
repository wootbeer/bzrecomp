"""Battlezone: Rise of the Black Dogs (USA) ROM layout, shared by the analysis tools.

The boot segment is loaded by IPL3 (entry 0x80028400 = ROM 0x1000). The game's
own segment table at 0x80057A30 (read by func_800295E0's main loop) lists three
modules that all load at 0x8010FFE0, one at a time: {vram, bss_end, rom_start,
rom_end_full, rom_end_text}. A negative module index loads only up to
rom_end_text (code, keeping the module's data).
"""
import struct

ROM_NAME = "Battlezone - Rise of the Black Dogs (USA).z64"

# name, rom_start, vram, rom_text_end (exclusive), rom_end (text+data), bss_end
SECTIONS = [
    ("boot", 0x001000, 0x80028400, None,     0x045540, 0x80110000),
    ("ovl0", 0x045540, 0x8010FFE0, 0x061058, 0x0DFDB0, 0x8023C310),
    ("ovl1", 0x0DFDB0, 0x8010FFE0, 0x18BD20, 0x1CCF20, 0x802D96B0),
    ("ovl2", 0x1CCF20, 0x8010FFE0, 0x1D0438, 0x1D0440, 0x80113500),
]


def load_rom(path=ROM_NAME):
    data = open(path, "rb").read()
    if data[:4] != b"\x80\x37\x12\x40":
        raise SystemExit(f"{path}: not a big-endian .z64 (run tools/normalize_rom.py)")
    return data


def word(rom, off):
    return struct.unpack(">I", rom[off:off + 4])[0]


# Not CPU code: RSP microcode linked into the boot segment's text. rspboot at
# 0x80037570, graphics ucodes from 0x80037640 (0x800389D0 and 0x80039B60 are
# the others the display-list setup picks from), and the audio ucode at
# 0x8003B420 (func_80034CA0's M_AUDTASK setup, data at 0x8006BD80 size 0x800),
# running up to the next CPU function, 0x8003C240.
NOT_CODE = [
    ("boot", 0x80037570, 0x8003C240),
]

# Names n64sym gets wrong or misses, checked by hand (they win over its output).
# Correct libultra names matter: N64Recomp replaces those functions with the
# runtime's own by name.
KNOWN_NAMES = {
    # controller.o, between contreaddata.o and __osContGetInitData; calls
    # __osPackRequestData, __osContGetInitData and __osSiRawStartDma.
    0x80049140: "osContInit",
    # n64sym names both identical PI/SI access pairs "__osSi*": the pair beside
    # osPiStartDma is the PI one.
    0x8003F924: "__osPiGetAccess",
    0x8003F990: "__osPiRelAccess",
    0x800517A4: "__osSiGetAccess",
    0x80051810: "__osSiRelAccess",
    # beside __osGetCause/__osSetSR; the other match (0x8003E554) is a copy in
    # the game's own exception code.
    0x8004E2A0: "__osGetSR",
    0x8003E554: None,
    # n64sym calls this __osRdbSend; it is the game's debug-output call,
    # (buf, len) -> len, writing through its own dev-hardware library
    # (0x8003C700-0x8003EB3C). Implemented natively (src/game/debug_output.cpp).
    0x8003C700: "bz_debug_write",
}

# Mission code blobs. ovl1 (the in-game module) loads one per mission into a
# fixed slot: func_80198D30 indexes the ROM-address table at 0x801F9C90 (ovl1
# data) by mission, and the caller (ovl1_func_80123D68) reads 0x5400 bytes to
# 0x801FD150 and calls through the blob's first word (its entry point). Blob
# code calls into ovl1 and the boot segment.
BLOB_SLOT = 0x801FD150
BLOB_SLOT_SIZE = 0x5400
BLOB_TABLE_ROM = 0x801F9C90 - 0x80030230  # in ovl1's data
BLOB_COUNT = 94  # entries; 44 distinct blobs (missions share some)
BLOB_LAST_END = 0x22DDE8  # the last blob runs to the next asset


def blob_starts(rom):
    """The distinct blob ROM addresses, in ROM order."""
    starts = set()
    for k in range(BLOB_COUNT):
        starts.add(word(rom, BLOB_TABLE_ROM + 4 * k) & 0x0FFFFFFF)
    return sorted(starts)


def all_sections(rom):
    """SECTIONS plus one section per mission blob ("misNN", in ROM order),
    and each section's parent (whose code it may call besides boot)."""
    secs = list(SECTIONS)
    parents = {}
    starts = blob_starts(rom)
    for i, a in enumerate(starts):
        end = starts[i + 1] if i + 1 < len(starts) else BLOB_LAST_END
        name = f"mis{i:02d}"
        secs.append((name, a, BLOB_SLOT, None, end, BLOB_SLOT + BLOB_SLOT_SIZE))
        parents[name] = "ovl1"
    return secs, parents
