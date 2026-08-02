#pragma once

// The Unicode emoji list, generated into EmojiTable.cpp by scripts/sync-emoji-table.sh. It is
// data, not content: ~1900 short strings, so it is compiled in rather than shipped as an addon.
// The glyphs themselves are not here — those are drawn with the addon font (EmojiCatalog).

namespace drift::emoji {

struct TableEntry
{
    const char *emoji;    // UTF-8, one fully-qualified sequence
    const char *name;     // Unicode name, e.g. "grinning face"
    const char *keywords; // subgroup, searched but not shown
    int group;            // index into kGroups
};

extern const char *const kGroups[];
extern const int kGroupCount;
extern const TableEntry kTable[];
extern const int kTableSize;

} // namespace drift::emoji
