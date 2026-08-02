#pragma once

#include "Time.h"

#include <QColor>
#include <QList>
#include <QString>

namespace drift {

enum class TextAlign { Left, Center, Right };
enum class TextVAlign { Top, Middle, Bottom };

// Entrance / exit motion. Every kind is expressible as opacity + offset + scale + blur on the
// finished text layer, which is what keeps the rasterized glyphs cacheable across frames.
// Typewriter is a hard binary reveal; Rise/Bounce add per-span overshoot; Wave is continuous
// (it oscillates for the whole clip rather than settling), and only ever reads with a per-span unit.
enum class TextAnimKind {
    None, Fade, SlideUp, SlideDown, SlideLeft, SlideRight, Pop, Blur, Typewriter, Rise, Bounce, Wave
};
enum class TextEase { Linear, EaseOut, EaseInOut, Back };

// Reveal granularity: Block animates the whole text at once (the original behaviour); the others
// stagger the entrance/exit across characters, words or lines for kinetic-typography reveals.
enum class TextAnimUnit { Block, Word, Character, Line };

// Order the staggered spans fire in.
enum class TextAnimOrder { Forward, Backward, CenterOut, Random };

// Which words a style pack accents. The positional rules are time-independent, so the raster
// stays cacheable for the whole cue; Karaoke follows the word being spoken and re-rasterizes as
// the playhead crosses each word.
enum class WordAccentRule { None, FirstWord, LastWord, EveryOther, EveryNth, LongestWord,
                            RandomStable, Karaoke };

QString textAlignToString(TextAlign align);
TextAlign textAlignFromString(const QString &align);

QString textVAlignToString(TextVAlign valign);
TextVAlign textVAlignFromString(const QString &valign);

QString textAnimKindToString(TextAnimKind kind);
TextAnimKind textAnimKindFromString(const QString &kind);

QString textEaseToString(TextEase ease);
TextEase textEaseFromString(const QString &ease);

QString textAnimUnitToString(TextAnimUnit unit);
TextAnimUnit textAnimUnitFromString(const QString &unit);

QString textAnimOrderToString(TextAnimOrder order);
TextAnimOrder textAnimOrderFromString(const QString &order);

QString wordAccentRuleToString(WordAccentRule rule);
WordAccentRule wordAccentRuleFromString(const QString &rule);

struct TextAnimation
{
    TextAnimKind kind = TextAnimKind::None;
    TimeUs durationUs = 400000;
    TextEase ease = TextEase::EaseOut;

    // Per-span reveal. Block (the default) keeps the original whole-layer motion; the other units
    // stagger the spans by staggerUs, ordered per `order`.
    TextAnimUnit unit = TextAnimUnit::Block;
    TimeUs staggerUs = 60000;
    TextAnimOrder order = TextAnimOrder::Forward;
};

// Rounded pill drawn behind a word. Used both for "every word" backgrounds and for the
// accent-only highlight a pack paints under its chosen words.
struct TextHighlight
{
    bool enabled = false;
    QColor color = QColor(230, 40, 40);
    double padding = 6.0; // px at pixelSize
    double radius = 4.0;
};

// The per-word override a style pack applies to the words its rule picks out. Everything left
// disabled falls through to the block style, so a pack only states what it changes.
struct WordAccent
{
    WordAccentRule rule = WordAccentRule::None;
    int n = 2;    // EveryNth stride
    int phase = 0; // index of the first accented word

    bool colorEnabled = false;
    QColor color = QColor(255, 214, 64);
    double sizeScale = 1.0; // relative to the block's pixelSize; changes the layout

    bool outlineEnabled = false;
    double outlineWidth = 0.0;
    QColor outlineColor = Qt::black;

    TextHighlight highlight;
};

struct TextStyle
{
    QString packId; // last applied style pack; cleared once the style is hand-edited
    QString fontFamily = QStringLiteral("Inter");
    int pixelSize = 64; // at project height
    int fontWeight = 700; // 100..900
    bool italic = false;
    QColor color = Qt::white;

    TextAlign align = TextAlign::Center;
    TextVAlign valign = TextVAlign::Middle;
    bool wordWrap = true;
    double lineHeight = 1.2; // multiple of the font's natural line spacing
    double letterSpacing = 0.0; // px at pixelSize

    double outlineWidth = 0.0; // px; 0 = no outline. Grows outward from the glyph edge.
    QColor outlineColor = Qt::black;

    bool shadowEnabled = false;
    double shadowOffsetX = 0.0;
    double shadowOffsetY = 4.0;
    double shadowBlur = 8.0;
    double shadowOpacity = 0.6;
    QColor shadowColor = QColor(0, 0, 0);

    // Offsetless coloured bloom. Separate from the shadow because the packs that use it want both
    // at once — a glow for the look and a shadow for legibility.
    bool glowEnabled = false;
    QColor glowColor = QColor(255, 255, 255);
    double glowRadius = 18.0;
    double glowOpacity = 0.8;

    bool boxEnabled = false; // filled background behind the whole block
    QColor boxColor = QColor(0, 0, 0, 128);
    double boxPadding = 8.0;
    double boxRadius = 0.0;

    TextHighlight wordHighlight; // pill behind every word

    bool underlineEnabled = false;
    QColor underlineColor = QColor(230, 40, 40);
    double underlineWidth = 6.0;
    double underlineOffset = 4.0; // below the baseline

    WordAccent accent;

    TextAnimation animIn;
    TextAnimation animOut;
};

struct TextPreset
{
    QString id;
    QString label;
    TextStyle style;
};

const QList<TextPreset> &textPresets();
const TextStyle *textStyleForPresetId(const QString &id);

} // namespace drift
