#!/usr/bin/env python3
"""Fill the characters 0xProto lacks with glyphs from a fallback monospace font.

OrcaSlicer draws its wxWidgets UI in the bundled 0xProto font. Native controls get no
per-character font fallback on every platform (Windows GDI draws a missing glyph as a box),
so characters the UI uses but 0xProto lacks (³ ² → ✓ ∆ ...) are copied into the bundled
0xProto files here, from JetBrains Mono, whose x-height and em size match.

Only codepoints missing from 0xProto are added: every glyph 0xProto has stays untouched.
Copied glyphs keep their outlines unscaled and are centred in 0xProto's wider advance.
Their hinting is dropped, since it calls the fallback font's own hinting functions.
Running the script again on its output adds nothing new.

Usage (fallback: JetBrains Mono 2.304, https://github.com/JetBrains/JetBrainsMono/releases/tag/v2.304):
    python3 scripts/merge_ui_font_fallback.py <dir with JetBrainsMono-Regular.ttf and JetBrainsMono-Bold.ttf>

Requires fontTools (pip install fonttools).
"""

import argparse
import copy
import sys
from pathlib import Path

from fontTools.ttLib import TTFont
from fontTools.ttLib.tables.ttProgram import Program

FONTS_DIR = Path(__file__).resolve().parent.parent / "resources" / "fonts"
WEIGHTS = {"0xProto-Regular.ttf": "JetBrainsMono-Regular.ttf", "0xProto-Bold.ttf": "JetBrainsMono-Bold.ttf"}
GLYPH_PREFIX = "fallback."
VERSION_NOTE = "; missing characters from JetBrains Mono"

# Characters the fallback font lacks either, drawn with a glyph the merged font already has.
ALIASES = {
    0x2714: 0x2713,  # ✔ heavy check mark   -> ✓
    0x2716: 0x2717,  # ✖ heavy multiplication x -> ✗
    0x202F: 0x0020,  # narrow no-break space -> space (monospace)
}


def is_wanted(cp: int) -> bool:
    # Private use areas hold font-specific icons (e.g. Powerline), not characters the UI asks for.
    # 0xProto only has BMP cmap tables, and the UI uses no characters beyond the BMP.
    return cp <= 0xFFFF and not 0xE000 <= cp <= 0xF8FF


def copy_glyph(name, src, dx, copied):
    """Copy glyph `name` of src (and its components) into `copied` under a prefixed name; return the new name."""
    new_name = GLYPH_PREFIX + name
    if new_name in copied:
        return new_name
    glyph = src["glyf"][name]
    glyph.expand(src["glyf"])
    glyph = copy.deepcopy(glyph)
    if glyph.isComposite():
        # The components are shifted themselves, so the composite keeps their offsets.
        for comp in glyph.components:
            comp.glyphName = copy_glyph(comp.glyphName, src, dx, copied)
    elif glyph.numberOfContours > 0:
        glyph.coordinates.translate((dx, 0))
    if hasattr(glyph, "program"):
        glyph.program = Program()
        glyph.program.fromBytecode(b"")
    copied[new_name] = (glyph, src["hmtx"][name][1] + dx)
    return new_name


def merge(base_path: Path, fallback_path: Path, out_path: Path) -> int:
    base = TTFont(base_path)
    fallback = TTFont(fallback_path)
    if base["head"].unitsPerEm != fallback["head"].unitsPerEm:
        sys.exit(f"{fallback_path.name}: units per em differ from {base_path.name}")

    base_cmap = base.getBestCmap()
    advance = base["hmtx"][base_cmap[ord("0")]][0]
    dx = (advance - fallback["hmtx"][fallback.getBestCmap()[ord("0")]][0]) // 2

    missing = {cp: name for cp, name in fallback.getBestCmap().items() if cp not in base_cmap and is_wanted(cp)}
    copied = {}
    new_map = {cp: copy_glyph(name, fallback, dx, copied) for cp, name in sorted(missing.items())}

    glyph_order = base.getGlyphOrder()
    base.setGlyphOrder(glyph_order + [n for n in copied if n not in glyph_order])
    for name, (glyph, lsb) in copied.items():
        base["glyf"][name] = glyph
        base["hmtx"][name] = (advance, lsb)

    full_map = dict(base_cmap)
    full_map.update(new_map)
    for cp, target in ALIASES.items():
        if cp not in full_map and target in full_map:
            new_map[cp] = full_map[target]
    for table in base["cmap"].tables:
        if table.isUnicode():
            for cp, name in new_map.items():
                table.cmap[cp] = name

    base["OS/2"].recalcUnicodeRanges(base)
    for record in base["name"].names:
        if record.nameID == 5 and VERSION_NOTE not in record.toUnicode():
            record.string = record.toUnicode() + VERSION_NOTE
    base.save(out_path)
    return len(new_map)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("fallback_dir", type=Path, help="directory holding the JetBrains Mono .ttf files")
    args = parser.parse_args()
    for base_name, fallback_name in WEIGHTS.items():
        added = merge(FONTS_DIR / base_name, args.fallback_dir / fallback_name, FONTS_DIR / base_name)
        print(f"{base_name}: added {added} characters")


if __name__ == "__main__":
    main()
