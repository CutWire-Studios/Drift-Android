#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// The full emoji set for the picker, on top of the curated sticker packs. The list itself is
// compiled in (EmojiTable), but the artwork is drawn with a colour emoji font, which arrives as
// the "emoji-font" kind of the sticker addon — the stickers are extracted from that same font, so
// shipping them apart would mean downloading the face twice. Without it the catalog is empty and
// the UI shows an install prompt, the same way the sticker tab does.
//
// Picked emoji become ordinary image clips — the glyph is rasterised once into
// <AppDataLocation>/emoji/<codepoints>.png and the clip points at that file, so nothing in the
// compositor has to know emoji exist.

struct EmojiEntry
{
    QString emoji; // the character sequence itself, and the clip's identity
    QString name;
    QString group;
    QString keywords; // extra search terms, not displayed
};

// Empty until an addon providing the emoji-font kind is installed.
QString emojiFontFamily();

// Filtered to sequences the installed font can actually draw, so a font that predates the table
// shows no tofu. Groups are in Unicode order and only include groups with surviving entries.
const QList<EmojiEntry> &emojiCatalog();
QStringList emojiGroups();

// Registers the addon font with QFontDatabase and rebuilds the catalog — GUI thread only, like
// reloadFontCatalog.
void reloadEmojiCatalog(const QStringList &packageRoots = {});

// Absolute path to the rasterised glyph, rendering it on first use. Empty when no font is
// installed. Regenerating is cheap and idempotent, which is what lets a project opened on another
// machine restore its emoji clips.
QString emojiImagePath(const QString &emoji);
