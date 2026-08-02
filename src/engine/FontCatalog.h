#pragma once

#include "core/TextStyle.h"

#include <QFont>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

// File-based font packages: fonts/<id>/{family.json, *.ttf, OFL.txt}, discovered the same way as
// effect and transition packages. The bundle is fetched by scripts/fetch-fonts.py rather than
// committed, so an empty catalog is a normal state — callers fall back to system fonts.

struct FontFace
{
    int weight = 400; // 100..900, as requested from Google Fonts
    bool italic = false;
    QString styleName; // e.g. "Black", "Bold Italic" — how the face is actually selected
    QString file;      // absolute path
};

struct FontFamilyEntry
{
    QString id;
    QString family;   // display name, e.g. "Plus Jakarta Sans"
    QString qtFamily; // family name Qt registered the faces under
    QString category; // impact | clean | editorial | playful
    QString license;
    QString packageDir;
    int order = 0;
    QList<FontFace> faces; // sorted by weight, upright before italic

    QList<int> weights() const; // distinct upright weights, ascending
    bool hasItalic() const;
};

const QList<FontFamilyEntry> &fontCatalog();
const FontFamilyEntry *fontFamilyForName(const QString &family);
QList<QPair<QString, QString>> fontCategories(); // id + label, display order

// Register the packages with QFontDatabase. Needs a QGuiApplication, so call it explicitly from
// main()/tools/tests rather than relying on the lazy path.
void reloadFontCatalog(const QStringList &packageRoots = {});

// Resolve a style to a concrete face at pixelSizePx. Falls through to the system font database for
// families that are not in the catalog, so projects written before the bundle still render.
QFont fontForStyle(const drift::TextStyle &style, int pixelSizePx);
