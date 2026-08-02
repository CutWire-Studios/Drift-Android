#include "EmojiCatalog.h"

#include "EmojiTable.h"
#include "GpuPackageParse.h"

#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QRawFont>
#include <QStandardPaths>

namespace {

constexpr int kRasterSize = 160; // matches the sticker packs, so emoji and stickers scale alike
// Fits the glyph's layout box, which is wider than its ink, so the drawn emoji lands at roughly
// the 140px the sticker art occupies inside the same 160px frame.
constexpr int kGlyphSize = 152;

QMutex g_mutex;
QList<EmojiEntry> g_catalog;
QStringList g_groups;
QString g_family;
int g_fontId = -1;
bool g_initialized = false;

// Sequences are glued together with joiners and selectors that fonts do not carry in their cmap;
// only the visible characters say anything about coverage.
bool isCoverageIrrelevant(char32_t point)
{
    return point == 0x200D                        // zero width joiner
        || point == 0xFE0E || point == 0xFE0F     // variation selectors
        || (point >= 0xE0020 && point <= 0xE007F); // tag characters (subdivision flags)
}

QString findFontFile(const QStringList &roots)
{
    const QStringList patterns{QStringLiteral("*.ttf"), QStringLiteral("*.otf"),
                               QStringLiteral("*.ttc")};
    for (const QString &root : roots) {
        QDir dir(root);
        if (!dir.exists())
            continue;
        // Loose in the root — which is what the sticker addon's emoji-font/ directory looks like,
        // and what a bare local override directory looks like — or one package directory down.
        const QStringList direct = dir.entryList(patterns, QDir::Files, QDir::Name);
        if (!direct.isEmpty())
            return dir.filePath(direct.constFirst());

        const QStringList packages = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &package : packages) {
            QDir packageDir(dir.filePath(package));
            const QStringList fonts = packageDir.entryList(patterns, QDir::Files, QDir::Name);
            if (!fonts.isEmpty())
                return packageDir.filePath(fonts.constFirst());
        }
    }
    return {};
}

void rebuildLocked(const QStringList &packageRoots)
{
    g_catalog.clear();
    g_groups.clear();
    g_initialized = true;

    if (g_fontId >= 0) {
        QFontDatabase::removeApplicationFont(g_fontId);
        g_fontId = -1;
    }
    g_family.clear();

    const QStringList roots = packageRoots.isEmpty()
        ? GpuPackageParse::defaultSearchPaths(QStringLiteral("DRIFT_EMOJI_FONT_DIR"),
                                              QStringLiteral("emoji-fonts"),
                                              QStringLiteral("emoji-font"))
        : packageRoots;

    const QString file = findFontFile(roots);
    if (file.isEmpty())
        return;

    g_fontId = QFontDatabase::addApplicationFont(file);
    if (g_fontId < 0)
        return;
    g_family = QFontDatabase::applicationFontFamilies(g_fontId).value(0);
    if (g_family.isEmpty()) {
        QFontDatabase::removeApplicationFont(g_fontId);
        g_fontId = -1;
        return;
    }

    QFont probe(g_family);
    probe.setPixelSize(64);
    const QRawFont raw = QRawFont::fromFont(probe);

    for (int i = 0; i < drift::emoji::kTableSize; ++i) {
        const drift::emoji::TableEntry &entry = drift::emoji::kTable[i];
        const QString emoji = QString::fromUtf8(entry.emoji);

        bool covered = true;
        for (const char32_t point : emoji.toUcs4()) {
            if (isCoverageIrrelevant(point))
                continue;
            if (!raw.supportsCharacter(point)) {
                covered = false;
                break;
            }
        }
        if (!covered)
            continue;

        const QString group = QString::fromUtf8(drift::emoji::kGroups[entry.group]);
        if (!g_groups.contains(group))
            g_groups.append(group);
        g_catalog.append(EmojiEntry{emoji, QString::fromUtf8(entry.name), group,
                                    QString::fromUtf8(entry.keywords)});
    }
}

void ensureLoadedLocked()
{
    if (!g_initialized)
        rebuildLocked({});
}

QString cacheFileName(const QString &emoji)
{
    QStringList parts;
    for (const char32_t point : emoji.toUcs4())
        parts.append(QString::number(point, 16));
    return parts.join(QLatin1Char('-')) + QStringLiteral(".png");
}

} // namespace

QString emojiFontFamily()
{
    QMutexLocker lock(&g_mutex);
    ensureLoadedLocked();
    return g_family;
}

const QList<EmojiEntry> &emojiCatalog()
{
    QMutexLocker lock(&g_mutex);
    ensureLoadedLocked();
    return g_catalog;
}

QStringList emojiGroups()
{
    QMutexLocker lock(&g_mutex);
    ensureLoadedLocked();
    return g_groups;
}

void reloadEmojiCatalog(const QStringList &packageRoots)
{
    QMutexLocker lock(&g_mutex);
    rebuildLocked(packageRoots);
}

QString emojiImagePath(const QString &emoji)
{
    if (emoji.isEmpty())
        return {};

    QString family;
    {
        QMutexLocker lock(&g_mutex);
        ensureLoadedLocked();
        family = g_family;
    }
    if (family.isEmpty())
        return {};

    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        return {};
    const QString dir = QDir(base).filePath(QStringLiteral("emoji"));
    const QString path = QDir(dir).filePath(cacheFileName(emoji));
    if (QFileInfo::exists(path))
        return path;
    QDir().mkpath(dir);

    // Fit the glyph to the raster rather than trusting a fixed point size: emoji vary in advance
    // and ascent between fonts, and a flag drawn at the same size as a face reads as smaller.
    QFont font(family);
    font.setPixelSize(kGlyphSize);
    QRectF bounds = QFontMetricsF(font).boundingRect(emoji);
    const qreal extent = qMax(bounds.width(), bounds.height());
    if (extent <= 0.0)
        return {};
    if (!qFuzzyCompare(extent, qreal(kGlyphSize))) {
        font.setPixelSize(qBound(8, qRound(kGlyphSize * kGlyphSize / extent), 512));
        bounds = QFontMetricsF(font).boundingRect(emoji);
    }

    QImage image(kRasterSize, kRasterSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setFont(font);
        const QPointF centre(kRasterSize / 2.0, kRasterSize / 2.0);
        painter.drawText(centre - bounds.center(), emoji);
    }

    if (!image.save(path, "PNG"))
        return {};
    return path;
}
