#!/usr/bin/env python3
"""
The rebuilt Gadgetron sign (tools/gen_sign.py) in Kalebo III's data (a stage of tools/patch_levels.py).

Class 1891's blob in the decoded core data is replaced in place. The new blob is larger, so its space grows by
whole 0x100-byte steps (every later block keeps its alignment) and every core offset past it moves up by as much
(level_data.core_pointers), and every texture whose pixels lie past it, the Charge Boots' among them
(level_data.shift_textures); the class stays inside the part of the core the engine keeps resident (below +0x7C),
where the level had it.
"""
import struct

from level_data import Level, core_pointers, shift_textures, table

LEVEL = 16            # Kalebo III, the only level with the sign
CLASS_ID = 1891
STEP = 0x100


def replace_sign(level: Level, blob: bytes) -> dict:
    """Kalebo III's sign class replaced by BLOB."""
    index = level.index
    _, _, classes = table(index, 0x18, 32)
    starts = [struct.unpack_from("<i", e)[0] for e in classes if struct.unpack_from("<i", e, 4)[0] == CLASS_ID]
    if len(starts) != 1 or starts[0] <= 0:
        raise SystemExit(f"level {level.number} has no class {CLASS_ID} blob")
    start = starts[0]
    pointers = core_pointers(index)
    end = min(o for _, o in pointers if o > start)
    resident_end, = struct.unpack_from("<I", index, 0x7C)
    if end > resident_end:
        raise SystemExit(f"class {CLASS_ID} at {start:#x}..{end:#x} "
                         f"is not in the resident part (ends {resident_end:#x})")
    old = end - start
    region = old + max(0, -(-(len(blob) - old) // STEP)) * STEP
    delta = region - old
    shifted = 0
    for where, offset in pointers:
        if offset >= end:
            struct.pack_into("<i", index, where, offset + delta)
            shifted += 1
    shifted += shift_textures(index, end, delta)    # the Charge Boots texture lies past the sign
    level.core = level.core[:start] + blob + bytes(region - len(blob)) + level.core[end:]
    return {"at": start, "old": old, "new": len(blob), "grew": delta, "shifted": shifted}
