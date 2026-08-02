#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <atomic>

#include <cmath>
#include <random>

#include "core/Clip.h"
#include "core/Project.h"
#include "engine/AudioMixer.h"
#include "engine/ClipReader.h"
#include "engine/Exporter.h"
#include "engine/CompositorFrameHistory.h"
#include "engine/AudioEffectCatalog.h"
#include "engine/audio/AudioEffectFactory.h"
#include "engine/audio/AudioEffectRack.h"
#include "engine/AudioFileWriter.h"
#include "engine/AudioOnsets.h"
#include "engine/DeepFilterDenoiser.h"
#include "engine/EffectCatalog.h"
#include "engine/EffectPackageLoader.h"
#include "engine/EffectProcessor.h"
#include "engine/FaceTrack.h"
#include "engine/EmojiCatalog.h"
#include "engine/FontCatalog.h"
#include "engine/FrameCompositor.h"
#include "engine/TextRaster.h"
#include "engine/GpuEffectExecutor.h"
#include "engine/MaskApplier.h"
#include "engine/MatteWriter.h"
#include "engine/ClipReaderPool.h"
#include "engine/TransitionCatalog.h"
#include "core/Transition.h"

class EngineTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void matteWriterRoundTripsThroughClipReader();
    void faceTrackRoundTripsAndInterpolates();
    void emojiCatalogNeedsFontAddon();
    void emojiRasterisesGlyph();
    void effectProcessorPassthroughWithoutEffects();
    void effectProcessorBrightness();
    void clipReaderSequentialAndSeek();
    void clipReaderAudioSequential();
    void compositorDefaultRenderStaysFullResolution();
    void compositorPreviewScaleRendersLowerResolution();
    void compositorPreviewScaleMapsProjectPixelLayout();
    void compositorAppliesFaceWarpFromBakedTrack();
    void compositorAppliesMultiplyBlendMode();
    void compositorAnimatesKeyedEffectParam();
    void compositorRendersShapeClip();
    void compositorSkipsClipBeingEdited();
    void adjustmentEffectContrastCatalogEntry();
    void effectPresetStableIds();
    void effectPresetCatalogIncludesStylizePresets();
    void effectBrowserCategories();
    void effectGraphTemplateSubstitution();
    void compositorOnlyPresetsUseCompositorPath();
    void effectPackageLoaderParsesGaussianBlur();
    void effectPackageLoaderRejectsReservedUniform();
    void effectPackageLoaderRejectsMissingShader();
    void gpuGaussianBlurChangesImage();
    void gpuMultiPassPreservesVerticalOrientation();
    void gpuBrokenShaderPassthrough();
    void rgbSplitZeroAmountPassthrough();
    void rgbSplitShiftsColorChannels();
    void blockGlitchDeterministicForSameTimeAndSeed();
    void blockGlitchChangesWithTimelineTime();
    void scanlineGlitchZeroStrengthPassthrough();
    void scanlineGlitchDeterministicAtFixedTime();
    void scanlineGlitchVisualChangeAtNonzeroSettings();
    void vhsCrtZeroSettingsPassthrough();
    void vhsCrtNonzeroModifiesOutput();
    void vhsCrtDeterministicAtFixedTime();
    void bloomGlowZeroIntensityPassthrough();
    void bloomGlowDarkFrameUnchanged();
    void bloomGlowBrightSpotBleedsToNeighbors();
    void rippleWaterZeroAmplitudePassthrough();
    void rippleWaterNonzeroDisplacementChangesOutput();
    void edgeNeonZeroIntensityUnchanged();
    void edgeNeonHighContrastRectangleGlow();
    void digitalGlitchZeroIntensityUnchanged();
    void digitalGlitchDeterministicForFixedTimeAndSeed();
    void filmBurnZeroIntensityUnchanged();
    void filmBurnAddsWarmLeakContribution();
    void timeEchoBlendDeterministic();
    void timeEchoBlendIncludesHistoryContribution();
    void timeEchoDeterministicAtFixedTimelineTime();
    void timeEchoBlendsPriorVideoFrames();
    void shockwavePulseZeroStrengthPassthrough();
    void shockwavePulseChangesPixelsNearWavefront();
    void compositorCrossfadeBetweenShapeClips();
    void compositorDipToBlackMidpointIsBlack();
    void compositorWipeRightRevealsIncomingClip();
    void transitionCatalogLoadsAllPackages();
    void gpuTransitionBindsBothSources();
    void brokenTransitionShaderFallsBackToCrossfade();
    void transitionRenderingIsDeterministic();
    void textClipRendersInsideTransition();
    void fontCatalogLoadsFamilies();
    void fontForStyleResolvesRequestedFace();
    void textRasterIsCached();
    void textDecorationsAreNotCropped();
    void wordAccentRecoloursChosenWords();
    void karaokeAccentFollowsThePlayhead();
    void accentSizeScaleWidensTheBlock();
    void everyStylePackRenders();
    void heavyWeightsRenderSolidGlyphs();
    void textClipCarriesGpuEffects();
    void textAnimationFadesAndSlides();
    void maskApplierEllipseMasksCorners();
    void exporterProducesPlayableFileWithBackground();
    void mixerHasNoBlockBoundaryDropout();
    void mixerSurvivesConcurrentEffectRackReset();
    void audioEffectCatalogLoadsPackages();
    void audioEffectFactoryBuildsEveryCatalogEntry();
    void audioEffectChainAltersSignal();
    void audioEffectChainBypassesUnknownEffect();
    void audioEffectStreamIsContinuousAcrossBlocks();
    void audioEffectFlangerProcessesSignal();
    void audioEffectRackPrimingAlignsLatentStages();
    void pitchShiftMovesPitchInTheRightDirection();
    void audioEffectRackParameterChangeIsContinuous();
    void onsetsDetectClickTrackTempo();
    void onsetsIgnoreSilence();
    void denoiseAuxiliaryConstantsRoundTrip();
    void denoisePreservesLengthAndSilence();
    void denoiseRemovesBroadbandNoise();
    void denoiseHasNoSeamAcrossWindows();
    void audioFileWriterRoundTripsThroughClipReader();

private:
    static QString makeColorSegmentsVideo(QTemporaryDir &dir);
    static QString makeToneAudio(QTemporaryDir &dir);
};

void EngineTest::initTestCase()
{
    const QString effectsDir = QString::fromUtf8(DRIFT_TEST_EFFECTS_DIR);
    QVERIFY2(QDir(effectsDir).exists(), qPrintable(effectsDir));
    reloadEffectCatalog({effectsDir});

    const QString transitionsDir = QString::fromUtf8(DRIFT_TEST_TRANSITIONS_DIR);
    QVERIFY2(QDir(transitionsDir).exists(), qPrintable(transitionsDir));
    reloadTransitionCatalog({transitionsDir});

    // The font bundle is fetched rather than committed, so an offline checkout legitimately has
    // none. The font tests skip in that case rather than fail.
    reloadFontCatalog({QString::fromUtf8(DRIFT_TEST_FONTS_DIR)});

    const QString audioEffectsDir = QString::fromUtf8(DRIFT_TEST_AUDIO_EFFECTS_DIR);
    QVERIFY2(QDir(audioEffectsDir).exists(), qPrintable(audioEffectsDir));
    reloadAudioEffectCatalog({audioEffectsDir});
}

// Without the emoji-font addon there is nothing to draw with, and offering a picker full of tofu
// is worse than offering none — so the catalog stays empty rather than falling back to the system.
void EngineTest::emojiCatalogNeedsFontAddon()
{
    QTemporaryDir empty;
    QVERIFY(empty.isValid());
    reloadEmojiCatalog({empty.path()});

    QVERIFY(emojiFontFamily().isEmpty());
    QVERIFY(emojiCatalog().isEmpty());
    QVERIFY(emojiGroups().isEmpty());
    QVERIFY(emojiImagePath(QString::fromUtf8("\xF0\x9F\x98\x80")).isEmpty());
}

void EngineTest::emojiRasterisesGlyph()
{
    // Like the font bundle, the emoji font is an addon rather than a checked-in asset.
    reloadEmojiCatalog({QString::fromUtf8(DRIFT_TEST_EMOJI_FONT_DIR)});
    if (emojiFontFamily().isEmpty())
        QSKIP("No emoji font available");

    QVERIFY(!emojiCatalog().isEmpty());
    QVERIFY(!emojiGroups().isEmpty());

    const QString grinning = QString::fromUtf8("\xF0\x9F\x98\x80");
    const QString path = emojiImagePath(grinning);
    QVERIFY(!path.isEmpty());

    QImage image(path);
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), QSize(160, 160));

    // A glyph that failed to render still writes a valid, entirely transparent PNG.
    bool painted = false;
    for (int y = 0; y < image.height() && !painted; ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) > 0) {
                painted = true;
                break;
            }
        }
    }
    QVERIFY(painted);

    reloadEmojiCatalog();
}

// The matte is written by us but read back by the ordinary video path, so the two ends have to
// agree on codec, pixel format and time base. A mismatch shows up as a mask that decodes black
// The sidecar is what preview and export both read, so a rounding or indexing slip here shows up
// as a warp that lags the face rather than as an error.
void EngineTest::faceTrackRoundTripsAndInterpolates()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("track.json"));

    drift::FaceTrack track;
    track.fps = 30;
    track.startSrcUs = drift::secondsToUs(1.0);
    for (int i = 0; i < 3; ++i) {
        drift::FaceAnchors a;
        a.valid = true;
        a.faceCenter = QPointF(0.25 + 0.25 * i, 0.5);
        a.leftEye = QPointF(0.2 + 0.25 * i, 0.4);
        a.faceRx = 0.1;
        a.faceRy = 0.12;
        a.angle = 0.2;
        a.eyeRadius = 0.02;
        a.score = 0.9;

        drift::FaceTrackFrame frame;
        frame.faces.append(a);
        // A second slot that drops out in the middle: sampling it there must report no face
        // rather than interpolating across the gap.
        drift::FaceAnchors second = a;
        second.valid = (i != 1);
        second.faceCenter = QPointF(0.8, 0.3);
        frame.faces.append(second);
        track.frames.append(frame);
    }

    QString error;
    QVERIFY2(drift::writeFaceTrack(path, track, &error), qPrintable(error));

    drift::FaceTrack loaded;
    QVERIFY2(drift::readFaceTrack(path, &loaded, &error), qPrintable(error));
    QCOMPARE(loaded.fps, 30);
    QCOMPARE(loaded.startSrcUs, drift::secondsToUs(1.0));
    QCOMPARE(loaded.frames.size(), 3);

    // Exactly on frame 1.
    const drift::FaceAnchors onFrame = loaded.sample(drift::kUsPerSecond / 30, 0);
    QVERIFY(onFrame.valid);
    QVERIFY(qAbs(onFrame.faceCenter.x() - 0.5) < 1e-4);

    // Halfway between frames 0 and 1 — the whole point of interpolating rather than snapping.
    const drift::FaceAnchors between = loaded.sample(drift::kUsPerSecond / 60, 0);
    QVERIFY(between.valid);
    QVERIFY(qAbs(between.faceCenter.x() - 0.375) < 1e-4);

    // Past the end clamps to the last frame instead of falling off.
    const drift::FaceAnchors after = loaded.sample(drift::secondsToUs(10.0), 0);
    QVERIFY(after.valid);
    QVERIFY(qAbs(after.faceCenter.x() - 0.75) < 1e-4);

    // The gap in slot 1: neither neighbour pair may produce a face.
    QVERIFY(!loaded.sample(drift::kUsPerSecond / 60, 1).valid);
    QVERIFY(!loaded.sample(drift::kUsPerSecond / 30, 1).valid);

    // A slot that was never baked is simply absent.
    QVERIFY(!loaded.sample(0, 3).valid);
}

// or lands on the wrong frame — silent, and only visible in the composite.
void EngineTest::matteWriterRoundTripsThroughClipReader()
{
    // MatteWriter encodes lossless H.264 and nothing else, so an LGPL FFmpeg (no x264) has
    // nothing to run this against. Drift's own packages ship a GPL build; this is for anyone
    // building against a distro's LGPL one.
    if (!Exporter::videoCodecById(QStringLiteral("h264")).value(QStringLiteral("available")).toBool())
        QSKIP("No H.264 encoder available in this FFmpeg build");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("matte.mp4"));
    const QSize size(320, 240);
    const int frames = 10;

    drift::MatteWriter writer;
    QString error;
    QVERIFY2(writer.open(path, size, 30, 1, &error), qPrintable(error));

    // Each frame covers a different horizontal band, so a frame-indexing error is detectable.
    for (int i = 0; i < frames; ++i) {
        QImage mask(size, QImage::Format_Grayscale8);
        mask.fill(0);
        QPainter p(&mask);
        p.fillRect(QRect(0, i * 20, size.width(), 20), Qt::white);
        p.end();
        QVERIFY2(writer.writeFrame(mask, &error), qPrintable(error));
    }
    QVERIFY2(writer.finish(&error), qPrintable(error));

    QVERIFY(QFileInfo::exists(path));
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".part")));

    for (int i = 0; i < frames; ++i) {
        // Sample the middle of each frame's interval: the boundary time can land a hair below it
        // and resolve to the previous frame.
        const drift::TimeUs us = (2 * drift::TimeUs(i) + 1) * drift::kUsPerSecond / 60;
        const QImage frame = ClipReaderPool::instance().readVideoFrame(path, us, 0, 0);
        QVERIFY2(!frame.isNull(), qPrintable(QStringLiteral("frame %1 did not decode").arg(i)));
        QCOMPARE(frame.size(), size);

        int band = -1;
        for (int b = 0; b < frames + 2; ++b) {
            if (qRed(frame.pixel(size.width() / 2, b * 20 + 10)) > 200) {
                band = b;
                break;
            }
        }
        QCOMPARE(band, i);
    }
}

void EngineTest::effectProcessorPassthroughWithoutEffects()
{
    QImage image(64, 64, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    const QImage out = EffectProcessor::applyEffects(image, {});
    QCOMPARE(out.size(), image.size());
}

void EngineTest::effectProcessorBrightness()
{
    QImage image(64, 64, QImage::Format_RGBA8888);
    image.fill(QColor(100, 100, 100));

    if (!GpuEffectExecutor::instance().isAvailable())
        QSKIP("OpenGL offscreen context unavailable");

    drift::Effect effect;
    effect.catalogId = QStringLiteral("adjust.brightness");
    effect.parameters.insert(QStringLiteral("brightness"), 0.2);

    const QImage out = EffectProcessor::applyEffects(image, {effect});
    QVERIFY(!out.isNull());
    const QRgb pixel = out.pixel(32, 32);
    QVERIFY(qRed(pixel) > 100 || qGreen(pixel) > 100 || qBlue(pixel) > 100);
}

// Builds a 3-second, 64x64 clip: red [0,1), green [1,2), blue [2,3), sparse
// keyframes so the sequential path differs meaningfully from a per-frame seek.
QString EngineTest::makeColorSegmentsVideo(QTemporaryDir &dir)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        return {};

    const QString out = dir.filePath(QStringLiteral("colors.mp4"));
    QStringList args{
        QStringLiteral("-y"),
        QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
        QStringLiteral("color=c=red:s=64x64:r=25:d=1"),
        QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
        QStringLiteral("color=c=lime:s=64x64:r=25:d=1"),
        QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
        QStringLiteral("color=c=blue:s=64x64:r=25:d=1"),
        QStringLiteral("-filter_complex"), QStringLiteral("[0][1][2]concat=n=3:v=1:a=0[v]"),
        QStringLiteral("-map"), QStringLiteral("[v]"),
        QStringLiteral("-c:v"), QStringLiteral("libx264"),
        QStringLiteral("-g"), QStringLiteral("25"),
        QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
        out,
    };

    QProcess proc;
    proc.start(ffmpeg, args);
    if (!proc.waitForFinished(30000) || proc.exitCode() != 0)
        return {};
    return QFileInfo::exists(out) ? out : QString{};
}

void EngineTest::clipReaderSequentialAndSeek()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = makeColorSegmentsVideo(dir);
    if (path.isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");

    ClipReader reader;
    QVERIFY(reader.open(path));
    QVERIFY(reader.hasVideo());

    auto dominant = [&](drift::TimeUs us) -> QChar {
        QImage frame;
        if (!reader.readVideoFrameAt(us, frame, 64, 64) || frame.isNull())
            return QChar('?');
        const QRgb p = frame.pixel(32, 32);
        if (qRed(p) >= qGreen(p) && qRed(p) >= qBlue(p))
            return QChar('R');
        if (qGreen(p) >= qRed(p) && qGreen(p) >= qBlue(p))
            return QChar('G');
        return QChar('B');
    };

    // Forward sequential requests exercise the no-seek fast path.
    QCOMPARE(dominant(500'000), QChar('R'));   // 0.5s
    QCOMPARE(dominant(700'000), QChar('R'));   // 0.7s, small forward step
    QCOMPARE(dominant(1'500'000), QChar('G')); // 1.5s
    QCOMPARE(dominant(2'500'000), QChar('B')); // 2.5s
    // Backward jump forces a keyframe reseek and must not return a stale frame.
    QCOMPARE(dominant(500'000), QChar('R'));
    QCOMPARE(dominant(1'500'000), QChar('G'));
}

QString EngineTest::makeToneAudio(QTemporaryDir &dir)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        return {};

    const QString out = dir.filePath(QStringLiteral("tone.wav"));
    QStringList args{
        QStringLiteral("-y"),
        QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
        QStringLiteral("sine=frequency=440:sample_rate=48000:duration=2"),
        QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"),
        out,
    };

    QProcess proc;
    proc.start(ffmpeg, args);
    if (!proc.waitForFinished(30000) || proc.exitCode() != 0)
        return {};
    return QFileInfo::exists(out) ? out : QString{};
}

// Sequential small buffers must reconstruct the same signal as one contiguous
// read. The old path re-seeked on every buffer, repeating/overlapping audio.
void EngineTest::clipReaderAudioSequential()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = makeToneAudio(dir);
    if (path.isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");

    constexpr int kRate = 48000;
    constexpr int kChunk = 1024;
    constexpr int kChunks = 20;
    constexpr int kTotal = kChunk * kChunks;
    constexpr drift::TimeUs kStartUs = 200'000;

    ClipReader ref;
    QVERIFY(ref.open(path));
    QVERIFY(ref.hasAudio());
    QVector<float> refBuf(kTotal * 2, 0.0f);
    QCOMPARE(ref.readAudioInterleaved(kStartUs, kTotal, kRate, refBuf.data()), kTotal);

    double sumSq = 0.0;
    for (float s : refBuf)
        sumSq += static_cast<double>(s) * s;
    QVERIFY(std::sqrt(sumSq / refBuf.size()) > 0.05); // audibly non-silent

    ClipReader seq;
    QVERIFY(seq.open(path));
    QVector<float> seqBuf;
    seqBuf.reserve(kTotal * 2);
    QVector<float> chunkBuf(kChunk * 2);
    drift::TimeUs t = kStartUs;
    for (int c = 0; c < kChunks; ++c) {
        const int n = seq.readAudioInterleaved(t, kChunk, kRate, chunkBuf.data());
        QVERIFY(n > 0);
        for (int i = 0; i < n * 2; ++i)
            seqBuf.append(chunkBuf[i]);
        t += static_cast<drift::TimeUs>(n) * drift::kUsPerSecond / kRate;
    }

    const int cmp = qMin(refBuf.size(), seqBuf.size());
    QVERIFY(cmp >= kTotal * 2 - kChunk * 2);
    double err = 0.0;
    for (int i = 0; i < cmp; ++i) {
        const double d = static_cast<double>(refBuf[i]) - seqBuf[i];
        err += d * d;
    }
    QVERIFY(std::sqrt(err / cmp) < 0.02);
}

void EngineTest::compositorDefaultRenderStaysFullResolution()
{
    drift::Project project;
    project.setResolution(192, 108);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Shape});

    drift::Clip clip;
    clip.id = QStringLiteral("shape");
    clip.type = drift::ClipType::Shape;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(1.0);
    clip.shapeStyle.kind = drift::ShapeKind::Rectangle;
    clip.shapeStyle.fill = Qt::red;
    project.tracks()[0].clips.append(clip);

    FrameCompositor compositor;
    compositor.setProject(&project);

    const QImage frame = compositor.compositeAt(0);
    QCOMPARE(frame.size(), QSize(192, 108));
}

void EngineTest::compositorPreviewScaleRendersLowerResolution()
{
    drift::Project project;
    project.setResolution(192, 108);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Shape});

    drift::Clip clip;
    clip.id = QStringLiteral("shape");
    clip.type = drift::ClipType::Shape;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(1.0);
    clip.shapeStyle.kind = drift::ShapeKind::Rectangle;
    clip.shapeStyle.fill = Qt::red;
    project.tracks()[0].clips.append(clip);

    FrameCompositor compositor;
    compositor.setProject(&project);

    FrameCompositor::RenderOptions options;
    options.previewScale = 0.5;
    options.maxTimeEchoHistoryFrames = 1;
    const QImage frame = compositor.compositeAt(0, options);
    QCOMPARE(frame.size(), QSize(96, 54));

    const QImage fullFrame = compositor.compositeAt(0);
    QCOMPARE(fullFrame.size(), QSize(192, 108));
}

void EngineTest::compositorPreviewScaleMapsProjectPixelLayout()
{
    // Project-pixel layout must be scaled onto the preview canvas so WYSIWYG
    // handles (which map project px → widget) stay aligned with the frame.
    drift::Project project;
    project.setResolution(200, 100);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Shape});

    drift::Clip clip;
    clip.id = QStringLiteral("shape");
    clip.type = drift::ClipType::Shape;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(1.0);
    clip.shapeStyle.kind = drift::ShapeKind::Rectangle;
    clip.shapeStyle.fill = Qt::red;
    clip.shapeStyle.strokeWidth = 0.0;
    clip.transformX.setKeyframe(0, 40.0);
    clip.transformY.setKeyframe(0, 20.0);
    clip.transformW.setKeyframe(0, 80.0);
    clip.transformH.setKeyframe(0, 40.0);
    project.tracks()[0].clips.append(clip);

    FrameCompositor compositor;
    compositor.setProject(&project);

    FrameCompositor::RenderOptions options;
    options.previewScale = 0.5;
    const QImage frame = compositor.compositeAt(0, options);
    QCOMPARE(frame.size(), QSize(100, 50));
    // Scaled layout: (20,10)-(60,30) on the half-res canvas.
    QVERIFY(frame.pixelColor(40, 20).red() > 200);
    QCOMPARE(frame.pixelColor(0, 0), QColor(0, 0, 0));
    QCOMPARE(frame.pixelColor(90, 40), QColor(0, 0, 0));
}

// Effects reach the screen through two different code paths — the CPU chain in EffectProcessor and
// the GPU chain in GpuCompositor — and only the CPU one was wired up at first, so face warps
// rendered in tools and exports while the preview showed nothing at all. This drives the whole
// compositor, which is what the preview uses.
void EngineTest::compositorAppliesFaceWarpFromBakedTrack()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Diagonal wedges: a warp has to move visibly different pixels around, which a flat or
    // radially symmetric image would hide.
    QImage source(64, 64, QImage::Format_RGBA8888);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x)
            source.setPixelColor(x, y, ((x / 8) + (y / 8)) % 2 ? Qt::white : QColor(20, 40, 200));
    }
    const QString imagePath = dir.filePath(QStringLiteral("src.png"));
    QVERIFY(source.save(imagePath, "PNG"));

    // A face filling most of the frame, so the warp covers a large share of the pixels.
    drift::FaceAnchors a;
    a.valid = true;
    a.faceCenter = QPointF(0.5, 0.5);
    a.leftEye = QPointF(0.38, 0.42);
    a.rightEye = QPointF(0.62, 0.42);
    a.noseTip = QPointF(0.5, 0.52);
    a.mouthCenter = QPointF(0.5, 0.66);
    a.mouthLeft = QPointF(0.42, 0.66);
    a.mouthRight = QPointF(0.58, 0.66);
    a.chin = QPointF(0.5, 0.8);
    a.forehead = QPointF(0.5, 0.2);
    a.faceRx = 0.3;
    a.faceRy = 0.35;
    a.angle = 0.0;
    a.eyeRadius = 0.05;
    a.score = 1.0;

    drift::FaceTrack track;
    track.fps = 30;
    drift::FaceTrackFrame frame;
    frame.faces.append(a);
    for (int i = 0; i < 4; ++i)
        track.frames.append(frame);

    const QString trackPath = dir.filePath(QStringLiteral("track.json"));
    QString error;
    QVERIFY2(drift::writeFaceTrack(trackPath, track, &error), qPrintable(error));

    auto composite = [&](bool attachTrack) {
        drift::Project project;
        project.setResolution(64, 64);
        project.tracks().clear();
        project.tracks().append(drift::Track{.type = drift::TrackType::Video});

        drift::Clip clip;
        clip.id = QStringLiteral("c");
        clip.type = drift::ClipType::Image;
        clip.path = imagePath;
        clip.timelineStart = 0;
        clip.timelineDuration = drift::secondsToUs(1.0);
        if (attachTrack)
            clip.faceTrackPath = trackPath;

        drift::Effect warp;
        warp.catalogId = QStringLiteral("face_swirl");
        warp.parameters.insert(QStringLiteral("twist"), 2.5);
        warp.parameters.insert(QStringLiteral("coverage"), 1.8);
        warp.parameters.insert(QStringLiteral("faceIndex"), 0);
        clip.effects.append(warp);

        project.tracks()[0].clips.append(clip);

        FrameCompositor compositor;
        compositor.setProject(&project);
        return compositor.compositeAt(0);
    };

    const QImage warped = composite(true);
    const QImage untracked = composite(false);
    QVERIFY(!warped.isNull());
    QVERIFY(!untracked.isNull());

    // Without a track the effect must be a clean pass-through, and with one it must actually bend
    // the picture. Comparing the two pins both directions at once.
    int differing = 0;
    for (int y = 0; y < warped.height(); ++y) {
        for (int x = 0; x < warped.width(); ++x)
            differing += warped.pixel(x, y) != untracked.pixel(x, y) ? 1 : 0;
    }
    QVERIFY2(differing > 200,
             qPrintable(QStringLiteral("face warp changed only %1 pixels — the compositor is not "
                                       "feeding anchors to the effect")
                            .arg(differing)));
}

void EngineTest::compositorAppliesMultiplyBlendMode()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    auto writeSolidImage = [&](const QString &name, QColor color) {
        QImage image(64, 64, QImage::Format_RGBA8888);
        image.fill(color);
        const QString path = dir.filePath(name);
        image.save(path, "PNG");
        return path;
    };

    auto compositeOverBackground = [&](QColor background) {
        drift::Project project;
        project.setResolution(64, 64);
        project.tracks().clear();
        project.tracks().append(drift::Track{.type = drift::TrackType::Shape});
        project.tracks().append(drift::Track{.type = drift::TrackType::Shape});

        // Index 0 is the topmost track and composites in front, so the
        // multiplied foreground goes on track 0 and the background on track 1.
        drift::Clip top;
        top.id = QStringLiteral("top");
        top.type = drift::ClipType::Image;
        top.path = writeSolidImage(QStringLiteral("top.png"), Qt::red);
        top.blendMode = drift::BlendMode::Multiply;
        top.timelineStart = 0;
        top.timelineDuration = drift::secondsToUs(1.0);
        project.tracks()[0].clips.append(top);

        drift::Clip bottom;
        bottom.id = QStringLiteral("bottom");
        bottom.type = drift::ClipType::Image;
        bottom.path = writeSolidImage(QStringLiteral("bottom.png"), background);
        bottom.timelineStart = 0;
        bottom.timelineDuration = drift::secondsToUs(1.0);
        project.tracks()[1].clips.append(bottom);

        FrameCompositor compositor;
        compositor.setProject(&project);
        return compositor.compositeAt(0);
    };

    const QImage overGreen = compositeOverBackground(Qt::green);
    QCOMPARE(overGreen.pixelColor(32, 32), QColor(0, 0, 0));

    const QImage overWhite = compositeOverBackground(Qt::white);
    QCOMPARE(overWhite.pixelColor(32, 32), QColor(255, 0, 0));
}

// A keyed effect parameter is resolved inside the compositor, which is the only place preview and
// export share. If the bake were done anywhere else the two could drift apart, so assert on the
// composited pixels rather than on the resolved parameter map.
void EngineTest::compositorAnimatesKeyedEffectParam()
{
    if (!GpuEffectExecutor::instance().isAvailable())
        QSKIP("OpenGL offscreen context unavailable");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QImage source(64, 64, QImage::Format_RGBA8888);
    source.fill(QColor(100, 100, 100));
    const QString path = dir.filePath(QStringLiteral("grey.png"));
    QVERIFY(source.save(path, "PNG"));

    drift::Project project;
    project.setResolution(64, 64);

    drift::Clip clip;
    clip.id = QStringLiteral("animated");
    clip.type = drift::ClipType::Image;
    clip.path = path;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(2.0);

    drift::Effect effect;
    effect.catalogId = QStringLiteral("adjust.brightness");
    // The static value is deliberately the *opposite* of the ramp, so a composite that ignored the
    // track would darken instead of brighten and the assertions below would fail.
    effect.parameters.insert(QStringLiteral("brightness"), -0.5);
    drift::KeyframeTrack<double> ramp;
    ramp.setKeyframe(0, 0.0);
    ramp.setKeyframe(drift::secondsToUs(2.0), 0.5);
    effect.paramKeyframes.insert(QStringLiteral("brightness"), ramp);
    clip.effects.append(effect);
    project.tracks()[0].clips.append(clip);

    FrameCompositor compositor;
    compositor.setProject(&project);

    const int atStart = qRed(compositor.compositeAt(0).pixel(32, 32));
    const int atMid = qRed(compositor.compositeAt(drift::secondsToUs(1.0)).pixel(32, 32));
    const int atEnd = qRed(compositor.compositeAt(drift::secondsToUs(1.999)).pixel(32, 32));

    QVERIFY(atStart < atMid);
    QVERIFY(atMid < atEnd);
    // brightness 0 at t=0 leaves the source untouched.
    QVERIFY(qAbs(atStart - 100) <= 2);
}

void EngineTest::compositorRendersShapeClip()
{
    drift::Project project;
    project.setResolution(128, 128);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Shape});

    drift::Clip clip;
    clip.id = QStringLiteral("shape");
    clip.type = drift::ClipType::Shape;
    clip.name = QStringLiteral("triangle");
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(1.0);
    clip.shapeStyle.kind = drift::ShapeKind::Triangle;
    clip.shapeStyle.fill = QColor(255, 0, 0);
    clip.shapeStyle.stroke = Qt::white;
    clip.shapeStyle.strokeWidth = 2.0;
    clip.transformX.setKeyframe(0, 32.0);
    clip.transformY.setKeyframe(0, 32.0);
    clip.transformW.setKeyframe(0, 64.0);
    clip.transformH.setKeyframe(0, 64.0);
    project.tracks()[0].clips.append(clip);

    FrameCompositor compositor;
    compositor.setProject(&project);
    const QImage frame = compositor.compositeAt(0);
    QVERIFY(!frame.isNull());
    QVERIFY(frame.pixelColor(64, 64).red() > 200);
    QVERIFY(frame.pixelColor(0, 0) == QColor(0, 0, 0));
}

// RenderOptions::skipClipId omits one clip from the frame. Used by in-place text
// editing on the preview, where the QML editor stands in for the baked raster.
void EngineTest::compositorSkipsClipBeingEdited()
{
    drift::Project project;
    project.setResolution(128, 128);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Shape});

    drift::Clip clip;
    clip.id = QStringLiteral("edited");
    clip.type = drift::ClipType::Shape;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(1.0);
    clip.shapeStyle.kind = drift::ShapeKind::Rectangle;
    clip.shapeStyle.fill = QColor(255, 0, 0);
    clip.transformX.setKeyframe(0, 32.0);
    clip.transformY.setKeyframe(0, 32.0);
    clip.transformW.setKeyframe(0, 64.0);
    clip.transformH.setKeyframe(0, 64.0);
    project.tracks()[0].clips.append(clip);

    FrameCompositor compositor;
    compositor.setProject(&project);

    // Rendered normally the clip covers the centre.
    const QImage shown = compositor.compositeAt(0);
    QVERIFY(!shown.isNull());
    QVERIFY(shown.pixelColor(64, 64).red() > 200);

    // Skipping it leaves the background showing through.
    FrameCompositor::RenderOptions options;
    options.skipClipId = QStringLiteral("edited");
    const QImage hidden = compositor.compositeAt(0, options);
    QVERIFY(!hidden.isNull());
    QCOMPARE(hidden.pixelColor(64, 64), QColor(0, 0, 0));

    // An unrelated id must not hide anything.
    options.skipClipId = QStringLiteral("someone-else");
    const QImage untouched = compositor.compositeAt(0, options);
    QVERIFY(untouched.pixelColor(64, 64).red() > 200);
}

void EngineTest::adjustmentEffectContrastCatalogEntry()
{
    const EffectPresetEntry *def = effectDefForId(QStringLiteral("adjust.contrast"));
    QVERIFY(def);
    QVERIFY(def->isGpu);
    QCOMPARE(def->meta.parameters.size(), 1);
    QCOMPARE(def->meta.parameters[0].key, QStringLiteral("contrast"));

    if (!GpuEffectExecutor::instance().isAvailable())
        QSKIP("OpenGL offscreen context unavailable");

    QImage image(64, 64, QImage::Format_RGBA8888);
    image.fill(QColor(180, 180, 180));

    drift::Effect effect;
    effect.catalogId = def->meta.id;
    effect.parameters.insert(def->meta.parameters[0].key, 2.0);

    const QImage out = EffectProcessor::applyEffects(image, {effect});
    QVERIFY(!out.isNull());
    const QRgb pixel = out.pixel(32, 32);
    QVERIFY(qRed(pixel) > 180 || qGreen(pixel) > 180 || qBlue(pixel) > 180);
}

void EngineTest::effectPresetStableIds()
{
    const QStringList ids = effectPresetIds();
    QVERIFY(ids.size() >= 16);

    // Ids are persisted in project files, so they are API. New effects are namespaced
    // ("category.name"); the bare ids below predate that and can never be renamed. Adding to this
    // list has to be a deliberate act — that is the point of the test.
    static const QSet<QString> legacyBareIds = {
        QStringLiteral("beat_shake"),      QStringLiteral("bling_sparkle"),
        QStringLiteral("block_glitch"),    QStringLiteral("bloom_glow"),
        QStringLiteral("bokeh_dream"),     QStringLiteral("cinematic_grade"),
        QStringLiteral("digital_glitch"),  QStringLiteral("droste_zoom"),
        QStringLiteral("duotone"),         QStringLiteral("edge_neon"),
        QStringLiteral("film_burn"),       QStringLiteral("halation"),
        QStringLiteral("halftone_comic"),  QStringLiteral("kaleidoscope"),
        QStringLiteral("lens_flare"),      QStringLiteral("light_leak"),
        QStringLiteral("lightning_sky"),   QStringLiteral("motion_trail"),
        QStringLiteral("oil_paint"),       QStringLiteral("rgb_split"),
        QStringLiteral("ripple_water"),    QStringLiteral("scanline_glitch"),
        QStringLiteral("shockwave_pulse"), QStringLiteral("sketch_pencil"),
        QStringLiteral("spin_blur"),       QStringLiteral("star_filter"),
        QStringLiteral("strobe_flash"),    QStringLiteral("super8_film"),
        QStringLiteral("time_echo"),       QStringLiteral("vhs_crt"),
        QStringLiteral("wave_warp"),       QStringLiteral("zoom_pulse"),
    };

    QSet<QString> seen;
    for (const QString &id : ids) {
        QVERIFY2(!id.isEmpty(), "preset id must not be empty");
        QVERIFY2(id.contains(QLatin1Char('.')) || legacyBareIds.contains(id)
                     || id.startsWith(QStringLiteral("face_")),
                 qPrintable(QStringLiteral("stable id: %1").arg(id)));
        QVERIFY2(!seen.contains(id), qPrintable(QStringLiteral("duplicate id: %1").arg(id)));
        seen.insert(id);

        const EffectPresetEntry *def = effectDefForId(id);
        QVERIFY2(def, qPrintable(QStringLiteral("missing catalog entry for %1").arg(id)));
        QCOMPARE(def->meta.id, id);
        QVERIFY2(!def->meta.displayName.isEmpty(),
                 qPrintable(QStringLiteral("display name missing for %1").arg(id)));
        QVERIFY2(!def->meta.category.isEmpty(),
                 qPrintable(QStringLiteral("category missing for %1").arg(id)));
    }
}

void EngineTest::effectPresetCatalogIncludesStylizePresets()
{
    const auto requirePreset = [&](const char *id, const char *displayName, const char *category,
                                   bool isGpu) {
        const EffectPresetEntry *def = effectDefForId(QString::fromLatin1(id));
        QVERIFY2(def, id);
        QCOMPARE(def->meta.displayName, QString::fromLatin1(displayName));
        QCOMPARE(def->meta.category, QString::fromLatin1(category));
        QCOMPARE(def->isGpu, isGpu);
    };

    requirePreset("rgb_split", "RGB Split", "glitch", true);
    requirePreset("block_glitch", "Block Glitch", "glitch", true);
    requirePreset("scanline_glitch", "Scanline Glitch", "glitch", true);
    requirePreset("vhs_crt", "VHS / CRT", "retro", true);
    requirePreset("film_burn", "Film Burn / Light Leak", "retro", true);
    requirePreset("stylize.vhs", "VHS", "retro", true);
    requirePreset("stylize.bloom", "Bloom", "dreamy", true);
    requirePreset("bloom_glow", "Bloom / Glow", "dreamy", true);
    requirePreset("edge_neon", "Edge Glow / Neon", "dreamy", true);
    requirePreset("time_echo", "Time Echo / Trail", "dreamy", true);
    requirePreset("stylize.ripple", "Ripple", "glitch", true);
    requirePreset("ripple_water", "Ripple / Water", "glitch", true);
    requirePreset("shockwave_pulse", "Shockwave / Pulse", "glitch", true);
    requirePreset("digital_glitch", "Digital Glitch", "glitch", true);
    requirePreset("adjust.contrast", "Contrast", "impact", true);
}

void EngineTest::effectBrowserCategories()
{
    const QList<QPair<QString, QString>> categories = effectCategories();
    QVERIFY(categories.size() >= 4);
    QCOMPARE(categories[0].first, QStringLiteral("glitch"));
    QCOMPARE(categories[0].second, QStringLiteral("Glitch & Distortion"));
    QCOMPARE(categories[1].first, QStringLiteral("retro"));
    QCOMPARE(categories[2].first, QStringLiteral("dreamy"));
    QCOMPARE(categories[3].first, QStringLiteral("impact"));

    QSet<QString> knownCategories;
    for (const auto &category : categories)
        knownCategories.insert(category.first);

    for (const QString &id : effectPresetIds()) {
        const EffectPresetEntry *def = effectDefForId(id);
        QVERIFY(def);
        QVERIFY2(knownCategories.contains(def->meta.category),
                 qPrintable(QStringLiteral("unknown category for %1: %2").arg(id, def->meta.category)));
        QVERIFY(!effectCategoryLabel(def->meta.category).isEmpty());
    }
}

void EngineTest::effectGraphTemplateSubstitution()
{
    // VHS is a GPU package now — no libavfilter graph template.
    const EffectPresetEntry *def = effectDefForId(QStringLiteral("stylize.vhs"));
    QVERIFY(def);
    QVERIFY(def->isGpu);
    QVERIFY(def->gpu.valid);
    QCOMPARE(def->graphTemplate, QString());

    drift::Effect vhs;
    vhs.catalogId = QStringLiteral("stylize.vhs");
    vhs.parameters.insert(QStringLiteral("noise"), 30.0);
    QCOMPARE(buildFilterGraphForEffect(vhs), QString());
}

void EngineTest::compositorOnlyPresetsUseCompositorPath()
{
    const EffectPresetEntry *bloom = effectDefForId(QStringLiteral("stylize.bloom"));
    QVERIFY(bloom);
    QVERIFY(bloom->isGpu);
    QVERIFY(bloom->gpu.valid);
    QCOMPARE(buildFilterGraphForEffect({.catalogId = bloom->meta.id}), QString());

    if (!GpuEffectExecutor::instance().isAvailable())
        QSKIP("OpenGL offscreen context unavailable");

    QImage image(32, 32, QImage::Format_RGBA8888);
    image.fill(QColor(200, 120, 80));

    drift::Effect effect;
    effect.catalogId = bloom->meta.id;
    effect.parameters.insert(QStringLiteral("intensity"), 1.0);
    effect.parameters.insert(QStringLiteral("radius"), 4.0);

    const QImage out = EffectProcessor::applyEffects(image, {effect});
    QVERIFY(!out.isNull());
    QVERIFY(out.pixel(16, 16) != image.pixel(16, 16));
}

void EngineTest::effectPackageLoaderParsesGaussianBlur()
{
    const QString pkg =
        QDir(QString::fromUtf8(DRIFT_TEST_EFFECTS_DIR)).filePath(QStringLiteral("gaussian_blur"));
    QString error;
    const EffectPresetEntry entry = EffectPackageLoader::loadPackage(pkg, &error);
    QVERIFY2(entry.gpu.valid, qPrintable(error));
    QVERIFY(entry.isGpu);
    QCOMPARE(entry.meta.id, QStringLiteral("builtin.effects.gaussian_blur"));
    QCOMPARE(entry.meta.displayName, QStringLiteral("Gaussian Blur (GPU)"));
    QCOMPARE(entry.meta.category, QStringLiteral("dreamy"));
    QCOMPARE(entry.meta.parameters.size(), 1);
    QCOMPARE(entry.meta.parameters[0].key, QStringLiteral("u_blurRadius"));
    QCOMPARE(entry.gpu.passes.size(), 2);
    QCOMPARE(entry.gpu.intermediateBuffers.size(), 1);

    const EffectPresetEntry *cataloged = effectDefForId(QStringLiteral("builtin.effects.gaussian_blur"));
    QVERIFY(cataloged);
    QVERIFY(cataloged->isGpu);
    QVERIFY(cataloged->gpu.valid);
    QCOMPARE(buildFilterGraphForEffect({.catalogId = cataloged->meta.id}), QString());
}

void EngineTest::effectPackageLoaderRejectsReservedUniform()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString pkg = dir.filePath(QStringLiteral("bad_reserved"));
    QVERIFY(QDir().mkpath(pkg));
    QFile json(QDir(pkg).filePath(QStringLiteral("effect.json")));
    QVERIFY(json.open(QIODevice::WriteOnly | QIODevice::Text));
    json.write(R"({
      "id": "test.reserved",
      "displayName": "Bad",
      "category": "dreamy",
      "backend": "gpu",
      "parameters": [{"identifier": "u_resolution", "displayName": "Res", "type": "float",
                     "defaultValue": 1, "minValue": 0, "maxValue": 2}],
      "pipeline": {"intermediateBuffers": [], "passes": [
        {"passIndex": 0, "fragmentShader": "x.frag",
         "inputs": [{"type": "source_texture"}], "output": {"type": "canvas"}}
      ]}
    })");
    json.close();
    QFile frag(QDir(pkg).filePath(QStringLiteral("x.frag")));
    QVERIFY(frag.open(QIODevice::WriteOnly | QIODevice::Text));
    frag.write("#version 330 core\nin vec2 v_texCoord; out vec4 fragColor;\n"
               "uniform sampler2D u_currentTexture;\nvoid main(){ fragColor = texture(u_currentTexture, v_texCoord); }\n");
    frag.close();

    QString error;
    const EffectPresetEntry entry = EffectPackageLoader::loadPackage(pkg, &error);
    QVERIFY(!entry.gpu.valid);
    QVERIFY(error.contains(QStringLiteral("reserved")));
}

void EngineTest::effectPackageLoaderRejectsMissingShader()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString pkg = dir.filePath(QStringLiteral("missing_shader"));
    QVERIFY(QDir().mkpath(pkg));
    QFile json(QDir(pkg).filePath(QStringLiteral("effect.json")));
    QVERIFY(json.open(QIODevice::WriteOnly | QIODevice::Text));
    json.write(R"({
      "id": "test.missing",
      "displayName": "Missing",
      "category": "dreamy",
      "backend": "gpu",
      "parameters": [],
      "pipeline": {"intermediateBuffers": [], "passes": [
        {"passIndex": 0, "fragmentShader": "nope.frag",
         "inputs": [{"type": "source_texture"}], "output": {"type": "canvas"}}
      ]}
    })");
    json.close();

    QString error;
    const EffectPresetEntry entry = EffectPackageLoader::loadPackage(pkg, &error);
    QVERIFY(!entry.gpu.valid);
    QVERIFY(error.contains(QStringLiteral("missing shader")));
}

void EngineTest::gpuGaussianBlurChangesImage()
{
    if (!GpuEffectExecutor::instance().isAvailable())
        QSKIP("OpenGL offscreen context unavailable");

    QImage image(64, 64, QImage::Format_RGBA8888);
    image.fill(Qt::black);
    for (int y = 20; y < 44; ++y) {
        for (int x = 20; x < 44; ++x)
            image.setPixel(x, y, qRgba(255, 255, 255, 255));
    }

    drift::Effect effect;
    effect.catalogId = QStringLiteral("builtin.effects.gaussian_blur");
    effect.parameters.insert(QStringLiteral("u_blurRadius"), 8.0);

    const QImage out = EffectProcessor::applyEffects(image, {effect});
    QCOMPARE(out.size(), image.size());
    // Edge of the white square should pick up blur (not pure black outside).
    const QRgb outside = out.pixel(10, 32);
    QVERIFY2(qRed(outside) > 0 || qGreen(outside) > 0 || qBlue(outside) > 0,
             "expected blur bleed outside the white square");
}

void EngineTest::gpuMultiPassPreservesVerticalOrientation()
{
    if (!GpuEffectExecutor::instance().isAvailable())
        QSKIP("OpenGL offscreen context unavailable");

    // Red band at top, blue at bottom — multi-pass blur must not swap them.
    QImage image(32, 32, QImage::Format_RGBA8888);
    for (int y = 0; y < 32; ++y) {
        const QRgb color = (y < 16) ? qRgba(255, 0, 0, 255) : qRgba(0, 0, 255, 255);
        for (int x = 0; x < 32; ++x)
            image.setPixel(x, y, color);
    }

    drift::Effect blur;
    blur.catalogId = QStringLiteral("builtin.effects.gaussian_blur");
    blur.parameters.insert(QStringLiteral("u_blurRadius"), 2.0);

    const QImage blurred = EffectProcessor::applyEffects(image, {blur});
    QCOMPARE(blurred.size(), image.size());
    QVERIFY2(qRed(blurred.pixel(16, 4)) > qBlue(blurred.pixel(16, 4)),
             "blur: top should stay predominantly red");
    QVERIFY2(qBlue(blurred.pixel(16, 27)) > qRed(blurred.pixel(16, 27)),
             "blur: bottom should stay predominantly blue");

    // Bloom composites an FBO blur buffer with the source texture — both must share Y.
    QImage bloomSrc(32, 32, QImage::Format_RGBA8888);
    bloomSrc.fill(QColor(0, 0, 0));
    for (int x = 8; x < 24; ++x)
        bloomSrc.setPixel(x, 4, qRgba(255, 255, 255, 255)); // bright bar near top only

    drift::Effect bloom;
    bloom.catalogId = QStringLiteral("bloom_glow");
    bloom.parameters.insert(QStringLiteral("threshold"), 0.4);
    bloom.parameters.insert(QStringLiteral("intensity"), 1.5);
    bloom.parameters.insert(QStringLiteral("blurRadius"), 4.0);

    const QImage bloomed = EffectProcessor::applyEffects(bloomSrc, {bloom});
    QCOMPARE(bloomed.size(), bloomSrc.size());
    const int topGlow = qRed(bloomed.pixel(16, 6)) + qGreen(bloomed.pixel(16, 6))
                        + qBlue(bloomed.pixel(16, 6));
    const int bottomGlow = qRed(bloomed.pixel(16, 28)) + qGreen(bloomed.pixel(16, 28))
                           + qBlue(bloomed.pixel(16, 28));
    QVERIFY2(topGlow > bottomGlow + 20,
             qPrintable(QStringLiteral(
                 "bloom glow should stay near the bright top bar, not mirrored to the bottom "
                 "(top=%1 bottom=%2)")
                            .arg(topGlow)
                            .arg(bottomGlow)));
}

void EngineTest::gpuBrokenShaderPassthrough()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = dir.path();
    const QString pkg = QDir(root).filePath(QStringLiteral("broken_gpu"));
    QVERIFY(QDir().mkpath(pkg));
    QFile json(QDir(pkg).filePath(QStringLiteral("effect.json")));
    QVERIFY(json.open(QIODevice::WriteOnly | QIODevice::Text));
    json.write(R"({
      "id": "test.broken_shader",
      "displayName": "Broken",
      "category": "dreamy",
      "backend": "gpu",
      "parameters": [],
      "pipeline": {"intermediateBuffers": [], "passes": [
        {"passIndex": 0, "fragmentShader": "bad.frag",
         "inputs": [{"type": "source_texture"}], "output": {"type": "canvas"}}
      ]}
    })");
    json.close();
    QFile frag(QDir(pkg).filePath(QStringLiteral("bad.frag")));
    QVERIFY(frag.open(QIODevice::WriteOnly | QIODevice::Text));
    frag.write("#version 330 core\nthis is not valid glsl!!!\n");
    frag.close();

    reloadEffectCatalog({root, QString::fromUtf8(DRIFT_TEST_EFFECTS_DIR)});
    const EffectPresetEntry *def = effectDefForId(QStringLiteral("test.broken_shader"));
    QVERIFY(def);
    QVERIFY(def->isGpu);

    if (!GpuEffectExecutor::instance().isAvailable()) {
        reloadEffectCatalog({QString::fromUtf8(DRIFT_TEST_EFFECTS_DIR)});
        QSKIP("OpenGL offscreen context unavailable");
    }

    QImage image(32, 32, QImage::Format_RGBA8888);
    image.fill(QColor(12, 34, 56));

    drift::Effect effect;
    effect.catalogId = def->meta.id;

    const QImage out = EffectProcessor::applyEffects(image, {effect});
    QCOMPARE(out.size(), image.size());
    QCOMPARE(out.pixel(16, 16), image.pixel(16, 16));

    // Restore catalog for subsequent tests.
    reloadEffectCatalog({QString::fromUtf8(DRIFT_TEST_EFFECTS_DIR)});
}

static QImage makeRedBlueSplitTestImage()
{
    QImage image(64, 32, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    for (int x = 0; x < 32; ++x) {
        for (int y = 0; y < 32; ++y)
            image.setPixel(x, y, qRgba(255, 0, 0, 255));
    }
    for (int x = 32; x < 64; ++x) {
        for (int y = 0; y < 32; ++y)
            image.setPixel(x, y, qRgba(0, 0, 255, 255));
    }
    return image;
}

void EngineTest::rgbSplitZeroAmountPassthrough()
{
    const QImage image = makeRedBlueSplitTestImage();

    drift::Effect effect;
    effect.catalogId = QStringLiteral("rgb_split");
    effect.parameters.insert(QStringLiteral("amount"), 0.0);
    effect.parameters.insert(QStringLiteral("angle"), 0.0);
    effect.parameters.insert(QStringLiteral("animated"), false);
    effect.parameters.insert(QStringLiteral("speed"), 1.0);

    const QImage out = EffectProcessor::applyEffects(image, {effect}, 0);
    QCOMPARE(out, image);
}

void EngineTest::rgbSplitShiftsColorChannels()
{
    const QImage image = makeRedBlueSplitTestImage();

    drift::Effect effect;
    effect.catalogId = QStringLiteral("rgb_split");
    effect.parameters.insert(QStringLiteral("amount"), 8.0);
    effect.parameters.insert(QStringLiteral("angle"), 0.0);
    effect.parameters.insert(QStringLiteral("animated"), false);

    const QImage out = EffectProcessor::applyEffects(image, {effect}, 0);
    QVERIFY(!out.isNull());

    const QRgb original = image.pixel(30, 16);
    QCOMPARE(qRed(original), 255);
    QCOMPARE(qGreen(original), 0);
    QCOMPARE(qBlue(original), 0);

    const QRgb shifted = out.pixel(30, 16);
    QVERIFY(shifted != original);
    QCOMPARE(qRed(shifted), 0);
    QCOMPARE(qGreen(shifted), 0);
    QCOMPARE(qBlue(shifted), 0);

    const EffectPresetEntry *def = effectDefForId(QStringLiteral("stylize.rgb_split"));
    QVERIFY(def);
    QCOMPARE(def->meta.id, QStringLiteral("rgb_split"));
    QCOMPARE(buildFilterGraphForEffect({.catalogId = QStringLiteral("rgb_split")}), QString());
}

static QImage makeBlockGlitchTestImage()
{
    QImage image(128, 64, QImage::Format_RGBA8888);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const int stripe = (x / 16) % 2;
            image.setPixel(x, y, qRgba(stripe ? 40 : 220, stripe ? 180 : 60, stripe ? 240 : 90, 255));
        }
    }
    return image;
}

static drift::Effect makeBlockGlitchEffect()
{
    drift::Effect effect;
    effect.catalogId = QStringLiteral("block_glitch");
    effect.parameters.insert(QStringLiteral("intensity"), 1.0);
    effect.parameters.insert(QStringLiteral("blockSize"), 16.0);
    effect.parameters.insert(QStringLiteral("shiftAmount"), 32.0);
    effect.parameters.insert(QStringLiteral("frequency"), 1.0);
    effect.parameters.insert(QStringLiteral("seed"), 42.0);
    return effect;
}

void EngineTest::blockGlitchDeterministicForSameTimeAndSeed()
{
    const QImage image = makeBlockGlitchTestImage();
    const drift::Effect effect = makeBlockGlitchEffect();
    constexpr drift::TimeUs timeUs = 750'000;

    const QImage first = EffectProcessor::applyEffects(image, {effect}, timeUs);
    const QImage second = EffectProcessor::applyEffects(image, {effect}, timeUs);
    QCOMPARE(first, second);

    const EffectPresetEntry *def = effectDefForId(QStringLiteral("block_glitch"));
    QVERIFY(def);
    QVERIFY(def->isGpu);
    QCOMPARE(buildFilterGraphForEffect({.catalogId = QStringLiteral("block_glitch")}), QString());
}

void EngineTest::blockGlitchChangesWithTimelineTime()
{
    const QImage image = makeBlockGlitchTestImage();
    const drift::Effect effect = makeBlockGlitchEffect();

    const QImage atT0 = EffectProcessor::applyEffects(image, {effect}, 0);
    const QImage atT1 = EffectProcessor::applyEffects(image, {effect}, 500'000);
    const QImage atT2 = EffectProcessor::applyEffects(image, {effect}, 1'000'000);

    QVERIFY(atT0 != atT1);
    QVERIFY(atT1 != atT2);

    drift::Effect otherSeed = effect;
    otherSeed.parameters.insert(QStringLiteral("seed"), 99.0);
    const QImage other = EffectProcessor::applyEffects(image, {otherSeed}, 500'000);
    QVERIFY(other != atT1);
}

static drift::Effect makeScanlineGlitchEffect()
{
    drift::Effect effect;
    effect.catalogId = QStringLiteral("scanline_glitch");
    effect.parameters.insert(QStringLiteral("jitter"), 0.5);
    effect.parameters.insert(QStringLiteral("lineStrength"), 0.5);
    effect.parameters.insert(QStringLiteral("colorShift"), 6.0);
    effect.parameters.insert(QStringLiteral("speed"), 2.0);
    return effect;
}

void EngineTest::scanlineGlitchZeroStrengthPassthrough()
{
    const QImage image = makeBlockGlitchTestImage();

    drift::Effect effect;
    effect.catalogId = QStringLiteral("scanline_glitch");
    effect.parameters.insert(QStringLiteral("jitter"), 0.0);
    effect.parameters.insert(QStringLiteral("lineStrength"), 0.0);
    effect.parameters.insert(QStringLiteral("colorShift"), 0.0);
    effect.parameters.insert(QStringLiteral("speed"), 2.0);

    const QImage out = EffectProcessor::applyEffects(image, {effect}, 500'000);
    QCOMPARE(out, image);
}

void EngineTest::scanlineGlitchDeterministicAtFixedTime()
{
    const QImage image = makeBlockGlitchTestImage();
    const drift::Effect effect = makeScanlineGlitchEffect();
    constexpr drift::TimeUs timeUs = 333'000;

    const QImage first = EffectProcessor::applyEffects(image, {effect}, timeUs);
    const QImage second = EffectProcessor::applyEffects(image, {effect}, timeUs);
    QCOMPARE(first, second);

    const EffectPresetEntry *def = effectDefForId(QStringLiteral("scanline_glitch"));
    QVERIFY(def);
    QVERIFY(def->isGpu);
    QCOMPARE(buildFilterGraphForEffect({.catalogId = QStringLiteral("scanline_glitch")}), QString());
}

void EngineTest::scanlineGlitchVisualChangeAtNonzeroSettings()
{
    const QImage image = makeBlockGlitchTestImage();
    const drift::Effect effect = makeScanlineGlitchEffect();

    const QImage out = EffectProcessor::applyEffects(image, {effect}, 250'000);
    QVERIFY(out != image);

    const QImage later = EffectProcessor::applyEffects(image, {effect}, 750'000);
    QVERIFY(later != out);
}

static QImage makeVhsCrtTestImage()
{
    QImage image(96, 64, QImage::Format_RGBA8888);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            image.setPixel(x, y, qRgba(40 + x, 80 + y * 2, 160 - x / 2, 255));
        }
    }
    return image;
}

static drift::Effect makeVhsCrtEffect()
{
    drift::Effect effect;
    effect.catalogId = QStringLiteral("vhs_crt");
    effect.parameters.insert(QStringLiteral("scanlines"), 0.5);
    effect.parameters.insert(QStringLiteral("noise"), 0.4);
    effect.parameters.insert(QStringLiteral("colorBleed"), 5.0);
    effect.parameters.insert(QStringLiteral("distortion"), 0.35);
    effect.parameters.insert(QStringLiteral("vignette"), 0.4);
    effect.parameters.insert(QStringLiteral("desaturation"), 0.25);
    return effect;
}

void EngineTest::vhsCrtZeroSettingsPassthrough()
{
    const QImage image = makeVhsCrtTestImage();

    drift::Effect effect;
    effect.catalogId = QStringLiteral("vhs_crt");
    effect.parameters.insert(QStringLiteral("scanlines"), 0.0);
    effect.parameters.insert(QStringLiteral("noise"), 0.0);
    effect.parameters.insert(QStringLiteral("colorBleed"), 0.0);
    effect.parameters.insert(QStringLiteral("distortion"), 0.0);
    effect.parameters.insert(QStringLiteral("vignette"), 0.0);
    effect.parameters.insert(QStringLiteral("desaturation"), 0.0);

    const QImage out = EffectProcessor::applyEffects(image, {effect}, 500'000);
    QCOMPARE(out, image);
}

void EngineTest::vhsCrtNonzeroModifiesOutput()
{
    const QImage image = makeVhsCrtTestImage();
    const drift::Effect effect = makeVhsCrtEffect();

    const QImage out = EffectProcessor::applyEffects(image, {effect}, 420'000);
    QVERIFY(out != image);

    const EffectPresetEntry *def = effectDefForId(QStringLiteral("vhs_crt"));
    QVERIFY(def);
    QVERIFY(def->isGpu);
    QCOMPARE(buildFilterGraphForEffect({.catalogId = QStringLiteral("vhs_crt")}), QString());
}

void EngineTest::vhsCrtDeterministicAtFixedTime()
{
    const QImage image = makeVhsCrtTestImage();
    const drift::Effect effect = makeVhsCrtEffect();
    constexpr drift::TimeUs timeUs = 420'000;

    const QImage first = EffectProcessor::applyEffects(image, {effect}, timeUs);
    const QImage second = EffectProcessor::applyEffects(image, {effect}, timeUs);
    QCOMPARE(first, second);

    const QImage otherTime = EffectProcessor::applyEffects(image, {effect}, 900'000);
    QVERIFY(otherTime != first);
}

static drift::Effect makeBloomGlowEffect()
{
    drift::Effect effect;
    effect.catalogId = QStringLiteral("bloom_glow");
    effect.parameters.insert(QStringLiteral("threshold"), 0.5);
    effect.parameters.insert(QStringLiteral("intensity"), 1.0);
    effect.parameters.insert(QStringLiteral("blurRadius"), 8.0);
    return effect;
}

void EngineTest::bloomGlowZeroIntensityPassthrough()
{
    QImage image(32, 32, QImage::Format_RGBA8888);
    image.fill(QColor(255, 255, 255));

    drift::Effect effect = makeBloomGlowEffect();
    effect.parameters.insert(QStringLiteral("intensity"), 0.0);

    const QImage out = EffectProcessor::applyEffects(image, {effect});
    QCOMPARE(out, image);
}

void EngineTest::bloomGlowDarkFrameUnchanged()
{
    QImage image(32, 32, QImage::Format_RGBA8888);
    image.fill(QColor(20, 25, 30));

    const drift::Effect effect = makeBloomGlowEffect();
    const QImage out = EffectProcessor::applyEffects(image, {effect});
    QCOMPARE(out, image);
}

void EngineTest::bloomGlowBrightSpotBleedsToNeighbors()
{
    if (!GpuEffectExecutor::instance().isAvailable())
        QSKIP("OpenGL offscreen context unavailable");

    QImage image(32, 32, QImage::Format_RGBA8888);
    image.fill(QColor(10, 10, 10));
    // A small bright block (not a single pixel) so separable blur keeps
    // measurable energy after H+V dilution in 8-bit.
    for (int y = 14; y <= 18; ++y) {
        for (int x = 14; x <= 18; ++x)
            image.setPixel(x, y, qRgba(255, 255, 255, 255));
    }

    const drift::Effect effect = makeBloomGlowEffect();
    const QImage out = EffectProcessor::applyEffects(image, {effect});
    QVERIFY(out != image);

    const QRgb center = out.pixel(16, 16);
    QVERIFY(qRed(center) > 200 || qGreen(center) > 200 || qBlue(center) > 200);

    // Glow should raise at least one pixel outside the bright block.
    bool bled = false;
    for (int dy = -6; dy <= 6 && !bled; ++dy) {
        for (int dx = -6; dx <= 6; ++dx) {
            const int x = 16 + dx;
            const int y = 16 + dy;
            if (x >= 14 && x <= 18 && y >= 14 && y <= 18)
                continue;
            const QRgb n = out.pixel(x, y);
            if (qRed(n) > 12 || qGreen(n) > 12 || qBlue(n) > 12) {
                bled = true;
                break;
            }
        }
    }
    QVERIFY2(bled, "expected bloom bleed into neighboring pixels");

    const EffectPresetEntry *def = effectDefForId(QStringLiteral("bloom_glow"));
    QVERIFY(def);
    QVERIFY(def->isGpu);
    QCOMPARE(buildFilterGraphForEffect({.catalogId = QStringLiteral("bloom_glow")}), QString());
}

static drift::Effect makeRippleWaterEffect()
{
    drift::Effect effect;
    effect.catalogId = QStringLiteral("ripple_water");
    effect.parameters.insert(QStringLiteral("amplitude"), 12.0);
    effect.parameters.insert(QStringLiteral("frequency"), 10.0);
    effect.parameters.insert(QStringLiteral("speed"), 1.0);
    effect.parameters.insert(QStringLiteral("centerX"), 0.5);
    effect.parameters.insert(QStringLiteral("centerY"), 0.5);
    return effect;
}

void EngineTest::rippleWaterZeroAmplitudePassthrough()
{
    const QImage image = makeBlockGlitchTestImage();

    drift::Effect effect = makeRippleWaterEffect();
    effect.parameters.insert(QStringLiteral("amplitude"), 0.0);

    const QImage out = EffectProcessor::applyEffects(image, {effect}, 500'000);
    QCOMPARE(out, image);
}

void EngineTest::rippleWaterNonzeroDisplacementChangesOutput()
{
    const QImage image = makeBlockGlitchTestImage();
    const drift::Effect effect = makeRippleWaterEffect();

    const QImage out = EffectProcessor::applyEffects(image, {effect}, 250'000);
    QVERIFY(out != image);

    const QImage later = EffectProcessor::applyEffects(image, {effect}, 750'000);
    QVERIFY(later != out);

    const EffectPresetEntry *def = effectDefForId(QStringLiteral("ripple_water"));
    QVERIFY(def);
    QVERIFY(def->isGpu);
    QCOMPARE(buildFilterGraphForEffect({.catalogId = QStringLiteral("ripple_water")}), QString());
}

static QImage makeHighContrastRectangleImage()
{
    QImage image(64, 64, QImage::Format_RGBA8888);
    image.fill(QColor(0, 0, 0));
    for (int y = 16; y < 48; ++y) {
        for (int x = 16; x < 48; ++x)
            image.setPixel(x, y, qRgba(255, 255, 255, 255));
    }
    return image;
}

static drift::Effect makeEdgeNeonEffect()
{
    drift::Effect effect;
    effect.catalogId = QStringLiteral("edge_neon");
    effect.parameters.insert(QStringLiteral("threshold"), 0.15);
    effect.parameters.insert(QStringLiteral("intensity"), 1.0);
    effect.parameters.insert(QStringLiteral("radius"), 4.0);
    effect.parameters.insert(QStringLiteral("color"), QStringLiteral("#00ffff"));
    return effect;
}

void EngineTest::edgeNeonZeroIntensityUnchanged()
{
    const QImage image = makeHighContrastRectangleImage();

    drift::Effect effect = makeEdgeNeonEffect();
    effect.parameters.insert(QStringLiteral("intensity"), 0.0);

    const QImage out = EffectProcessor::applyEffects(image, {effect});
    QCOMPARE(out, image);
}

void EngineTest::edgeNeonHighContrastRectangleGlow()
{
    const QImage image = makeHighContrastRectangleImage();
    const drift::Effect effect = makeEdgeNeonEffect();

    const QImage out = EffectProcessor::applyEffects(image, {effect});
    QVERIFY(out != image);

    const QRgb outside = out.pixel(14, 32);
    QVERIFY(qGreen(outside) > qGreen(image.pixel(14, 32)));
    QVERIFY(qBlue(outside) > qBlue(image.pixel(14, 32)));

    const QRgb inside = out.pixel(32, 32);
    QCOMPARE(qRed(inside), 255);
    QCOMPARE(qGreen(inside), 255);
    QCOMPARE(qBlue(inside), 255);

    const EffectPresetEntry *def = effectDefForId(QStringLiteral("edge_neon"));
    QVERIFY(def);
    QVERIFY(def->isGpu);
    QCOMPARE(def->fixedParams.value(QStringLiteral("color")).toString(), QStringLiteral("#00ffff"));
    QCOMPARE(buildFilterGraphForEffect({.catalogId = QStringLiteral("edge_neon")}), QString());
}

static drift::Effect makeDigitalGlitchEffect()
{
    drift::Effect effect;
    effect.catalogId = QStringLiteral("digital_glitch");
    effect.parameters.insert(QStringLiteral("intensity"), 0.75);
    effect.parameters.insert(QStringLiteral("frequency"), 0.5);
    effect.parameters.insert(QStringLiteral("rgbAmount"), 12.0);
    effect.parameters.insert(QStringLiteral("blockAmount"), 0.6);
    effect.parameters.insert(QStringLiteral("flashAmount"), 0.25);
    effect.parameters.insert(QStringLiteral("seed"), 42.0);
    return effect;
}

void EngineTest::digitalGlitchZeroIntensityUnchanged()
{
    const QImage image = makeBlockGlitchTestImage();

    drift::Effect effect = makeDigitalGlitchEffect();
    effect.parameters.insert(QStringLiteral("intensity"), 0.0);

    const QImage out = EffectProcessor::applyEffects(image, {effect}, 500'000);
    QCOMPARE(out, image);
}

void EngineTest::digitalGlitchDeterministicForFixedTimeAndSeed()
{
    const QImage image = makeBlockGlitchTestImage();
    const drift::Effect effect = makeDigitalGlitchEffect();
    constexpr drift::TimeUs timeUs = 620'000;

    const QImage first = EffectProcessor::applyEffects(image, {effect}, timeUs);
    const QImage second = EffectProcessor::applyEffects(image, {effect}, timeUs);
    QCOMPARE(first, second);
    QVERIFY(first != image);

    const EffectPresetEntry *def = effectDefForId(QStringLiteral("digital_glitch"));
    QVERIFY(def);
    QVERIFY(def->isGpu);
    QCOMPARE(buildFilterGraphForEffect({.catalogId = QStringLiteral("digital_glitch")}), QString());

    drift::Effect otherSeed = effect;
    otherSeed.parameters.insert(QStringLiteral("seed"), 99.0);
    const QImage other = EffectProcessor::applyEffects(image, {otherSeed}, timeUs);
    QVERIFY(other != first);
}

static drift::Effect makeFilmBurnEffect()
{
    drift::Effect effect;
    effect.catalogId = QStringLiteral("film_burn");
    effect.parameters.insert(QStringLiteral("intensity"), 0.8);
    effect.parameters.insert(QStringLiteral("warmth"), 0.85);
    effect.parameters.insert(QStringLiteral("flicker"), 0.4);
    effect.parameters.insert(QStringLiteral("position"), QStringLiteral("left"));
    effect.parameters.insert(QStringLiteral("seed"), 7.0);
    return effect;
}

void EngineTest::filmBurnZeroIntensityUnchanged()
{
    QImage image(64, 64, QImage::Format_RGBA8888);
    image.fill(QColor(30, 30, 40));

    drift::Effect effect = makeFilmBurnEffect();
    effect.parameters.insert(QStringLiteral("intensity"), 0.0);

    const QImage out = EffectProcessor::applyEffects(image, {effect}, 400'000);
    QCOMPARE(out, image);
}

void EngineTest::filmBurnAddsWarmLeakContribution()
{
    QImage image(64, 64, QImage::Format_RGBA8888);
    image.fill(QColor(20, 22, 35));

    const drift::Effect effect = makeFilmBurnEffect();
    constexpr drift::TimeUs timeUs = 400'000;

    const QImage out = EffectProcessor::applyEffects(image, {effect}, timeUs);
    QVERIFY(out != image);

    const QRgb edge = out.pixel(0, 32);
    const QRgb original = image.pixel(0, 32);
    QVERIFY(qRed(edge) > qRed(original));
    QVERIFY(qGreen(edge) > qGreen(original));
    QVERIFY(qRed(edge) > qBlue(edge));

    const QImage second = EffectProcessor::applyEffects(image, {effect}, timeUs);
    QCOMPARE(out, second);

    const EffectPresetEntry *def = effectDefForId(QStringLiteral("film_burn"));
    QVERIFY(def);
    QVERIFY(def->isGpu);
    QCOMPARE(def->fixedParams.value(QStringLiteral("position")).toString(), QStringLiteral("left"));
    QCOMPARE(buildFilterGraphForEffect({.catalogId = QStringLiteral("film_burn")}), QString());
}

void EngineTest::timeEchoBlendDeterministic()
{
    auto makeFrame = [](const QColor &color, int rectX) {
        QImage image(32, 32, QImage::Format_RGBA8888);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.fillRect(rectX, 10, 10, 10, color);
        return image;
    };

    const QList<QImage> samples = {makeFrame(Qt::red, 18), makeFrame(Qt::blue, 4)};
    const QImage first =
        CompositorFrameHistory::applyTimeEcho(samples, 0.55, CompositorFrameHistory::EchoBlendMode::Normal);
    const QImage second =
        CompositorFrameHistory::applyTimeEcho(samples, 0.55, CompositorFrameHistory::EchoBlendMode::Normal);
    QCOMPARE(first, second);
    QVERIFY(first != samples.first());
    QVERIFY(qBlue(first.pixel(8, 14)) > 0);
    QVERIFY(qRed(first.pixel(22, 14)) > 200);
}

void EngineTest::timeEchoBlendIncludesHistoryContribution()
{
    auto makeFrame = [](const QColor &color, int rectX) {
        QImage image(32, 32, QImage::Format_RGBA8888);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.fillRect(rectX, 10, 10, 10, color);
        return image;
    };

    const QList<QImage> samples = {makeFrame(Qt::red, 18), makeFrame(Qt::blue, 4)};
    const QImage normal =
        CompositorFrameHistory::applyTimeEcho(samples, 0.5, CompositorFrameHistory::EchoBlendMode::Normal);
    const QImage currentOnly =
        CompositorFrameHistory::applyTimeEcho({samples.first()}, 0.5, CompositorFrameHistory::EchoBlendMode::Normal);
    QVERIFY(normal != currentOnly);
    QVERIFY(qBlue(normal.pixel(8, 14)) > qBlue(currentOnly.pixel(8, 14)));
}

static drift::Effect makeTimeEchoEffect(const QString &blendMode = QStringLiteral("add"))
{
    drift::Effect effect;
    effect.catalogId = QStringLiteral("time_echo");
    effect.parameters.insert(QStringLiteral("frames"), 4);
    effect.parameters.insert(QStringLiteral("decay"), 0.55);
    effect.parameters.insert(QStringLiteral("blendMode"), blendMode);
    return effect;
}

void EngineTest::timeEchoDeterministicAtFixedTimelineTime()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = makeColorSegmentsVideo(dir);
    if (path.isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");

    drift::Project project;
    project.setResolution(64, 64);
    project.setFps(25);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});

    drift::Clip clip;
    clip.id = QStringLiteral("video");
    clip.type = drift::ClipType::Video;
    clip.path = path;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(3.0);
    clip.effects.append(makeTimeEchoEffect());
    project.tracks()[0].clips.append(clip);

    FrameCompositor compositor;
    compositor.setProject(&project);

    constexpr drift::TimeUs timeUs = drift::secondsToUs(2.1);
    const QImage first = compositor.compositeAt(timeUs);
    const QImage second = compositor.compositeAt(timeUs);
    QCOMPARE(first, second);
    QVERIFY(!first.isNull());
}

void EngineTest::timeEchoBlendsPriorVideoFrames()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = makeColorSegmentsVideo(dir);
    if (path.isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");

    drift::Project project;
    project.setResolution(64, 64);
    project.setFps(25);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});

    drift::Clip clip;
    clip.id = QStringLiteral("video");
    clip.type = drift::ClipType::Video;
    clip.path = path;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(3.0);
    project.tracks()[0].clips.append(clip);

    FrameCompositor compositor;
    compositor.setProject(&project);

    constexpr drift::TimeUs timeUs = drift::secondsToUs(2.1);
    const QImage withoutEcho = compositor.compositeAt(timeUs);

    clip.effects.append(makeTimeEchoEffect(QStringLiteral("add")));
    project.tracks()[0].clips.clear();
    project.tracks()[0].clips.append(clip);
    compositor.setProject(&project);

    const QImage withEcho = compositor.compositeAt(timeUs);
    QVERIFY(withEcho != withoutEcho);

    const EffectPresetEntry *def = effectDefForId(QStringLiteral("time_echo"));
    QVERIFY(def);
    QVERIFY(def->isGpu);
    QCOMPARE(def->fixedParams.value(QStringLiteral("blendMode")).toString(), QStringLiteral("normal"));
    QCOMPARE(buildFilterGraphForEffect({.catalogId = QStringLiteral("time_echo")}), QString());
}

static drift::Effect makeShockwavePulseEffect()
{
    drift::Effect effect;
    effect.catalogId = QStringLiteral("shockwave_pulse");
    effect.parameters.insert(QStringLiteral("centerX"), 0.5);
    effect.parameters.insert(QStringLiteral("centerY"), 0.5);
    effect.parameters.insert(QStringLiteral("radius"), 0.0);
    effect.parameters.insert(QStringLiteral("width"), 0.12);
    effect.parameters.insert(QStringLiteral("strength"), 0.6);
    effect.parameters.insert(QStringLiteral("speed"), 1.0);
    return effect;
}

void EngineTest::shockwavePulseZeroStrengthPassthrough()
{
    const QImage image = makeBlockGlitchTestImage();

    drift::Effect effect = makeShockwavePulseEffect();
    effect.parameters.insert(QStringLiteral("strength"), 0.0);

    const QImage out = EffectProcessor::applyEffects(image, {effect}, 500'000);
    QCOMPARE(out, image);
}

void EngineTest::shockwavePulseChangesPixelsNearWavefront()
{
    const QImage image = makeBlockGlitchTestImage();
    const drift::Effect effect = makeShockwavePulseEffect();

    // speed=1 => wave radius 0.233 at t=233ms; pixel (80,32) lies on that ring from center (64,32).
    constexpr drift::TimeUs timeUs = 233'000;
    const QImage out = EffectProcessor::applyEffects(image, {effect}, timeUs);
    QVERIFY(out != image);
    QVERIFY(out.pixel(80, 32) != image.pixel(80, 32));

    const QImage awayFromWave = EffectProcessor::applyEffects(image, {effect}, 50'000);
    QVERIFY(awayFromWave.pixel(80, 32) != out.pixel(80, 32));

    const EffectPresetEntry *def = effectDefForId(QStringLiteral("shockwave_pulse"));
    QVERIFY(def);
    QVERIFY(def->isGpu);
    QCOMPARE(buildFilterGraphForEffect({.catalogId = QStringLiteral("shockwave_pulse")}), QString());
}

void EngineTest::compositorCrossfadeBetweenShapeClips()
{
    drift::Project project;
    project.setResolution(128, 128);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});

    drift::Clip clipA;
    clipA.id = QStringLiteral("a");
    clipA.type = drift::ClipType::Shape;
    clipA.timelineStart = 0;
    clipA.timelineDuration = drift::secondsToUs(2.0);
    clipA.shapeStyle.kind = drift::ShapeKind::Rectangle;
    clipA.shapeStyle.fill = Qt::red;

    drift::Clip clipB;
    clipB.id = QStringLiteral("b");
    clipB.type = drift::ClipType::Shape;
    clipB.timelineStart = drift::secondsToUs(2.0);
    clipB.timelineDuration = drift::secondsToUs(2.0);
    clipB.shapeStyle.kind = drift::ShapeKind::Rectangle;
    clipB.shapeStyle.fill = Qt::blue;

    project.tracks()[0].clips.append(clipA);
    project.tracks()[0].clips.append(clipB);

    drift::Transition transition;
    transition.id = QStringLiteral("tr");
    transition.fromClipId = clipA.id;
    transition.toClipId = clipB.id;
    transition.kindId = QStringLiteral("crossfade");
    transition.durationUs = drift::secondsToUs(1.0);
    project.tracks()[0].transitions.append(transition);

    FrameCompositor compositor;
    compositor.setProject(&project);

    const QImage redOnly = compositor.compositeAt(drift::secondsToUs(1.0));
    const QImage blueOnly = compositor.compositeAt(drift::secondsToUs(3.0));
    const QImage mid = compositor.compositeAt(drift::secondsToUs(2.0));
    QVERIFY(!mid.isNull());
    const QRgb center = mid.pixel(64, 64);
    const QRgb redCenter = redOnly.pixel(64, 64);
    const QRgb blueCenter = blueOnly.pixel(64, 64);
    QVERIFY(center != redCenter);
    QVERIFY(center != blueCenter);
    QVERIFY(qRed(center) > 0);
    QVERIFY(qBlue(center) > 0);
}

static void appendRedBlueShapeTransition(drift::Project &project, const QString &kindId)
{
    project.setResolution(128, 128);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});

    drift::Clip clipA;
    clipA.id = QStringLiteral("a");
    clipA.type = drift::ClipType::Shape;
    clipA.timelineStart = 0;
    clipA.timelineDuration = drift::secondsToUs(2.0);
    clipA.shapeStyle.kind = drift::ShapeKind::Rectangle;
    clipA.shapeStyle.fill = Qt::red;

    drift::Clip clipB;
    clipB.id = QStringLiteral("b");
    clipB.type = drift::ClipType::Shape;
    clipB.timelineStart = drift::secondsToUs(2.0);
    clipB.timelineDuration = drift::secondsToUs(2.0);
    clipB.shapeStyle.kind = drift::ShapeKind::Rectangle;
    clipB.shapeStyle.fill = Qt::blue;

    project.tracks()[0].clips.append(clipA);
    project.tracks()[0].clips.append(clipB);

    drift::Transition transition;
    transition.id = QStringLiteral("tr");
    transition.fromClipId = clipA.id;
    transition.toClipId = clipB.id;
    transition.kindId = kindId;
    transition.durationUs = drift::secondsToUs(1.0);
    project.tracks()[0].transitions.append(transition);
}

void EngineTest::compositorDipToBlackMidpointIsBlack()
{
    drift::Project project;
    appendRedBlueShapeTransition(project, QStringLiteral("dip"));

    FrameCompositor compositor;
    compositor.setProject(&project);

    const QImage mid = compositor.compositeAt(drift::secondsToUs(2.0));
    QVERIFY(!mid.isNull());
    const QRgb center = mid.pixel(64, 64);
    QVERIFY(qRed(center) < 30);
    QVERIFY(qGreen(center) < 30);
    QVERIFY(qBlue(center) < 30);
}

void EngineTest::compositorWipeRightRevealsIncomingClip()
{
    drift::Project project;
    appendRedBlueShapeTransition(project, QStringLiteral("wipe_right"));

    FrameCompositor compositor;
    compositor.setProject(&project);

    const QImage early = compositor.compositeAt(drift::secondsToUs(1.75));
    const QImage late = compositor.compositeAt(drift::secondsToUs(2.25));
    QVERIFY(!early.isNull());
    QVERIFY(!late.isNull());
    // Shape clips are small and centered; sample canvas center, not edges.
    QVERIFY(qRed(early.pixel(64, 64)) > qBlue(early.pixel(64, 64)));
    QVERIFY(qBlue(late.pixel(64, 64)) > qRed(late.pixel(64, 64)));
}

void EngineTest::transitionCatalogLoadsAllPackages()
{
    const QStringList ids = transitionPresetIds();
    QCOMPARE(ids.size(), 28);

    // The nine ids the pre-shader enum serialized must all still resolve.
    for (const char *legacy : {"crossfade", "dip", "dip_white", "wipe_left", "wipe_right",
                               "wipe_up", "wipe_down", "push_left", "zoom_in"}) {
        const TransitionPresetEntry *def = transitionDefForId(QString::fromUtf8(legacy));
        QVERIFY2(def, legacy);
        QVERIFY2(def->gpu.valid, legacy);
    }

    QCOMPARE(transitionDefForId(QStringLiteral("dip"))->audioCurve, QStringLiteral("dip"));
    QCOMPARE(transitionDefForId(QStringLiteral("crossfade"))->audioCurve,
             QStringLiteral("crossfade"));

    // matrix_rain is the one package with a static texture asset.
    const TransitionPresetEntry *rain = transitionDefForId(QStringLiteral("matrix_rain"));
    QVERIFY(rain);
    QCOMPARE(rain->gpu.textures.size(), 1);
    QVERIFY(QFileInfo::exists(rain->gpu.textures.first().path));
}

// Two solid layers through the real GPU path: the shader must actually receive source 1 as
// u_toTexture, not a second copy of source 0.
void EngineTest::gpuTransitionBindsBothSources()
{
    if (!GpuEffectExecutor::instance().isAvailable())
        QSKIP("no OpenGL context available");

    QImage red(64, 64, QImage::Format_RGBA8888);
    red.fill(Qt::red);
    QImage blue(64, 64, QImage::Format_RGBA8888);
    blue.fill(Qt::blue);

    const TransitionPresetEntry *def = transitionDefForId(QStringLiteral("crossfade"));
    QVERIFY(def);

    // Returns a null image if the pipeline reported failure, so the QVERIFYs stay in the test body.
    auto run = [&](double p) -> QImage {
        bool ok = false;
        const QImage out = GpuEffectExecutor::instance().apply(
            QLatin1String(kTransitionCacheKeyPrefix) + def->meta.id, def->gpu, {red, blue}, {}, 0, p,
            &ok);
        return ok ? out : QImage();
    };

    const QImage atStart = run(0.0);
    const QImage atEnd = run(1.0);
    const QImage atMid = run(0.5);
    QVERIFY(!atStart.isNull() && !atEnd.isNull() && !atMid.isNull());

    const QRgb start = atStart.pixel(32, 32);
    QVERIFY(qRed(start) > 240 && qBlue(start) < 15);

    const QRgb end = atEnd.pixel(32, 32);
    QVERIFY(qBlue(end) > 240 && qRed(end) < 15);

    const QRgb mid = atMid.pixel(32, 32);
    QVERIFY(qRed(mid) > 100 && qRed(mid) < 155);
    QVERIFY(qBlue(mid) > 100 && qBlue(mid) < 155);
}

// A broken shader must fall back to a CPU crossfade, never to a black frame or to clip A alone.
void EngineTest::brokenTransitionShaderFallsBackToCrossfade()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString pkg = QDir(dir.path()).filePath(QStringLiteral("broken"));
    QVERIFY(QDir().mkpath(pkg));

    QFile frag(QDir(pkg).filePath(QStringLiteral("main.frag")));
    QVERIFY(frag.open(QIODevice::WriteOnly));
    frag.write("#version 330 core\nthis is not glsl\n");
    frag.close();

    QFile json(QDir(pkg).filePath(QStringLiteral("transition.json")));
    QVERIFY(json.open(QIODevice::WriteOnly));
    json.write(R"({"id":"broken","displayName":"Broken","pipeline":{"passes":[{"passIndex":0,
        "fragmentShader":"main.frag","inputs":[{"type":"source_texture","index":0},
        {"type":"source_texture","index":1}],"output":{"type":"canvas"}}]}})");
    json.close();

    reloadTransitionCatalog({dir.path()});

    drift::Project project;
    appendRedBlueShapeTransition(project, QStringLiteral("broken"));
    FrameCompositor compositor;
    compositor.setProject(&project);

    const QImage mid = compositor.compositeAt(drift::secondsToUs(2.0));
    QVERIFY(!mid.isNull());
    const QRgb center = mid.pixel(64, 64);
    QVERIFY(qRed(center) > 0);
    QVERIFY(qBlue(center) > 0);

    reloadTransitionCatalog({QString::fromUtf8(DRIFT_TEST_TRANSITIONS_DIR)});
}

// The old CPU path never handled ClipType::Text inside drawTransitionFrame, and the main draw
// loop skipped both transition clips — so a text clip in a transition simply disappeared.
// Rendering each side into its own full-canvas layer routes text through the normal path.
void EngineTest::textClipRendersInsideTransition()
{
    drift::Project project;
    project.setResolution(128, 128);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});

    drift::Clip text;
    text.id = QStringLiteral("a");
    text.type = drift::ClipType::Text;
    text.timelineStart = 0;
    text.timelineDuration = drift::secondsToUs(2.0);
    text.textContent = QStringLiteral("HELLO");
    text.textStyle.color = Qt::white;
    text.textStyle.pixelSize = 28;

    drift::Clip shape;
    shape.id = QStringLiteral("b");
    shape.type = drift::ClipType::Shape;
    shape.timelineStart = drift::secondsToUs(2.0);
    shape.timelineDuration = drift::secondsToUs(2.0);
    shape.shapeStyle.kind = drift::ShapeKind::Rectangle;
    shape.shapeStyle.fill = Qt::blue;

    project.tracks()[0].clips.append(text);
    project.tracks()[0].clips.append(shape);

    drift::Transition transition;
    transition.id = QStringLiteral("tr");
    transition.fromClipId = text.id;
    transition.toClipId = shape.id;
    transition.kindId = QStringLiteral("crossfade");
    transition.durationUs = drift::secondsToUs(1.0);
    project.tracks()[0].transitions.append(transition);

    FrameCompositor compositor;
    compositor.setProject(&project);

    // Early in the window the text still dominates: some pixel must be lit by the glyphs.
    const QImage frame = compositor.compositeAt(drift::secondsToUs(1.6));
    QVERIFY(!frame.isNull());

    int lit = 0;
    for (int y = 0; y < frame.height(); ++y) {
        for (int x = 0; x < frame.width(); ++x) {
            const QRgb px = frame.pixel(x, y);
            if (qRed(px) > 150 && qGreen(px) > 150 && qBlue(px) > 150)
                ++lit;
        }
    }
    QVERIFY2(lit > 0, "text clip vanished inside the transition window");
}

// Transitions must be pure functions of (A, B, progress). If any shader smuggled in
// frame-to-frame state, rendering a later frame first would change this frame's output —
// which would also make the exporter disagree with the preview.
void EngineTest::transitionRenderingIsDeterministic()
{
    if (!GpuEffectExecutor::instance().isAvailable())
        QSKIP("no OpenGL context available");

    for (const QString &kindId : transitionPresetIds()) {
        drift::Project project;
        appendRedBlueShapeTransition(project, kindId);

        FrameCompositor compositor;
        compositor.setProject(&project);

        const QImage first = compositor.compositeAt(drift::secondsToUs(1.75));

        // Jump forward, then scrub back to the same time.
        compositor.compositeAt(drift::secondsToUs(2.4));
        const QImage rescrubbed = compositor.compositeAt(drift::secondsToUs(1.75));

        QVERIFY2(first == rescrubbed, qPrintable(kindId));
    }
}

namespace {

// The bundle is fetched, not committed, so an offline checkout legitimately has no fonts.
#define SKIP_WITHOUT_FONTS()                                                                        \
    do {                                                                                            \
        if (fontCatalog().isEmpty())                                                                \
            QSKIP("font bundle not present — run scripts/fetch-fonts.py");                          \
    } while (false)

drift::Clip makeTextClip(const QString &text, const QRectF &rect)
{
    drift::Clip clip;
    clip.id = QStringLiteral("text");
    clip.type = drift::ClipType::Text;
    clip.textContent = text;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(4.0);
    clip.transformX.setKeyframe(0, rect.x());
    clip.transformY.setKeyframe(0, rect.y());
    clip.transformW.setKeyframe(0, rect.width());
    clip.transformH.setKeyframe(0, rect.height());
    return clip;
}

int litPixels(const QImage &image)
{
    int lit = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) > 8)
                ++lit;
        }
    }
    return lit;
}

double meanLuminance(const QImage &image)
{
    double sum = 0.0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb px = image.pixel(x, y);
            sum += 0.2126 * qRed(px) + 0.7152 * qGreen(px) + 0.0722 * qBlue(px);
        }
    }
    return sum / (image.width() * image.height());
}

// Vertical centre of mass of the lit pixels — how the slide is observed.
double litCentroidY(const QImage &image)
{
    double weighted = 0.0;
    double total = 0.0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb px = image.pixel(x, y);
            const double lum = 0.2126 * qRed(px) + 0.7152 * qGreen(px) + 0.0722 * qBlue(px);
            weighted += lum * y;
            total += lum;
        }
    }
    return total > 0.0 ? weighted / total : 0.0;
}

} // namespace

void EngineTest::fontCatalogLoadsFamilies()
{
    SKIP_WITHOUT_FONTS();

    const QList<FontFamilyEntry> &families = fontCatalog();
    QVERIFY2(families.size() >= 20, qPrintable(QString::number(families.size())));

    for (const FontFamilyEntry &entry : families) {
        QVERIFY2(!entry.qtFamily.isEmpty(), qPrintable(entry.family));
        QVERIFY2(!entry.faces.isEmpty(), qPrintable(entry.family));
        QVERIFY2(!entry.category.isEmpty(), qPrintable(entry.family));
    }

    const FontFamilyEntry *montserrat = fontFamilyForName(QStringLiteral("Montserrat"));
    QVERIFY(montserrat);
    QVERIFY(montserrat->weights().size() >= 6);
    QVERIFY(montserrat->hasItalic());

    // Display faces really do ship a single weight — the picker must not invent more.
    const FontFamilyEntry *anton = fontFamilyForName(QStringLiteral("Anton"));
    QVERIFY(anton);
    QCOMPARE(anton->weights().size(), 1);
    QVERIFY(!anton->hasItalic());

    QVERIFY(fontFamilyForName(QStringLiteral("Pacifico")));
    QVERIFY(!fontFamilyForName(QStringLiteral("No Such Family")));
}

void EngineTest::fontForStyleResolvesRequestedFace()
{
    SKIP_WITHOUT_FONTS();

    drift::TextStyle style;
    style.fontFamily = QStringLiteral("Montserrat");
    style.fontWeight = 900;
    QCOMPARE(fontForStyle(style, 40).styleName(), QStringLiteral("Black"));

    style.fontWeight = 400;
    QCOMPARE(fontForStyle(style, 40).styleName(), QStringLiteral("Regular"));

    // 250 is not a real face; the nearest one in the requested direction wins.
    style.fontWeight = 250;
    QCOMPARE(fontForStyle(style, 40).styleName(), QStringLiteral("ExtraLight"));

    // Anton has no italic, so the upright face stands in rather than Qt faking an oblique.
    style.fontFamily = QStringLiteral("Anton");
    style.fontWeight = 400;
    style.italic = true;
    QVERIFY(!fontForStyle(style, 40).italic());

    // Families outside the bundle still resolve through the system database.
    style.fontFamily = QStringLiteral("Sans Serif");
    QCOMPARE(fontForStyle(style, 40).pixelSize(), 40);
}

void EngineTest::textRasterIsCached()
{
    SKIP_WITHOUT_FONTS();

    const QRectF rect(0, 0, 400, 200);
    drift::Clip clip = makeTextClip(QStringLiteral("Cache me"), rect);
    clip.textStyle.fontFamily = QStringLiteral("Inter");

    const TextRasterResult first = rasterizeText(clip, rect, 1.0);
    const TextRasterResult second = rasterizeText(clip, rect, 1.0);
    QVERIFY(!first.image.isNull());
    // Same underlying QImage, so the second frame did no rasterization at all.
    QCOMPARE(first.image.cacheKey(), second.image.cacheKey());

    // The animation is applied to the layer, never to the pixels, so it must not evict the raster.
    clip.textStyle.animIn = {drift::TextAnimKind::Fade, drift::secondsToUs(1.0), drift::TextEase::EaseOut};
    const TextRasterResult animated = rasterizeText(clip, rect, 1.0);
    QCOMPARE(animated.image.cacheKey(), first.image.cacheKey());

    clip.textStyle.color = Qt::red;
    QVERIFY(rasterizeText(clip, rect, 1.0).image.cacheKey() != first.image.cacheKey());
}

void EngineTest::textDecorationsAreNotCropped()
{
    SKIP_WITHOUT_FONTS();

    const QRectF rect(100, 100, 300, 80);
    drift::Clip clip = makeTextClip(QStringLiteral("Edge"), rect);
    clip.textStyle.fontFamily = QStringLiteral("Anton");
    clip.textStyle.pixelSize = 64;

    const TextRasterResult plain = rasterizeText(clip, rect, 1.0);
    QVERIFY(!plain.image.isNull());

    clip.textStyle.outlineWidth = 12.0;
    clip.textStyle.shadowEnabled = true;
    clip.textStyle.shadowBlur = 10.0;
    clip.textStyle.shadowOffsetY = 8.0;
    const TextRasterResult decorated = rasterizeText(clip, rect, 1.0);

    // The raster grows past the layout rect on every side, so nothing is clipped at the edge...
    QVERIFY(decorated.rect.width() > rect.width());
    QVERIFY(decorated.rect.height() > rect.height());
    QVERIFY(decorated.rect.left() < rect.left());
    QVERIFY(decorated.rect.top() < rect.top());
    // ...and the destination rect follows the image, so it still lands where the user put it.
    QCOMPARE(decorated.rect.center(), rect.center());
    QCOMPARE(decorated.image.width(), qRound(decorated.rect.width()));
    QCOMPARE(decorated.image.height(), qRound(decorated.rect.height()));

    // The stroke and shadow really do add ink.
    QVERIFY(litPixels(decorated.image) > litPixels(plain.image));

    // The same has to hold for the decorations a style pack adds, which sit outside the glyphs:
    // a glow bleeds outward, a highlight pill sits behind the word and the rule sits under it.
    clip.textStyle.outlineWidth = 0.0;
    clip.textStyle.shadowEnabled = false;
    clip.textStyle.glowEnabled = true;
    clip.textStyle.glowRadius = 14.0;
    clip.textStyle.wordHighlight.enabled = true;
    clip.textStyle.wordHighlight.padding = 10.0;
    clip.textStyle.underlineEnabled = true;
    clip.textStyle.underlineOffset = 10.0;
    clip.textStyle.underlineWidth = 8.0;
    const TextRasterResult packed = rasterizeText(clip, rect, 1.0);
    QCOMPARE(packed.rect.center(), rect.center());
    QCOMPARE(packed.image.width(), qRound(packed.rect.width()));
    QCOMPARE(packed.image.height(), qRound(packed.rect.height()));
    QVERIFY(litPixels(packed.image) > litPixels(plain.image));
}

namespace {

// Pixels close enough to pure red to have come from the accent colour rather than antialiasing.
int redPixels(const QImage &image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb px = image.pixel(x, y);
            if (qAlpha(px) > 200 && qRed(px) > 200 && qGreen(px) < 80 && qBlue(px) < 80)
                ++count;
        }
    }
    return count;
}

QRect inkBounds(const QImage &image)
{
    int left = image.width(), right = -1, top = image.height(), bottom = -1;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) <= 8)
                continue;
            left = qMin(left, x);
            right = qMax(right, x);
            top = qMin(top, y);
            bottom = qMax(bottom, y);
        }
    }
    return right < 0 ? QRect() : QRect(QPoint(left, top), QPoint(right, bottom));
}

} // namespace

void EngineTest::wordAccentRecoloursChosenWords()
{
    SKIP_WITHOUT_FONTS();

    const QRectF rect(0, 0, 700, 200);
    drift::Clip clip = makeTextClip(QStringLiteral("Number of thumbnails that"), rect);
    clip.textStyle.fontFamily = QStringLiteral("Inter");
    clip.textStyle.fontWeight = 800;
    clip.textStyle.pixelSize = 48;

    const QImage plain = rasterizeText(clip, rect, 1.0).image;
    QVERIFY(!plain.isNull());
    QCOMPARE(redPixels(plain), 0);

    clip.textStyle.accent.rule = drift::WordAccentRule::FirstWord;
    clip.textStyle.accent.colorEnabled = true;
    clip.textStyle.accent.color = QColor(255, 0, 0);
    const QImage accented = rasterizeText(clip, rect, 1.0).image;

    // Only the picked word changes colour: some ink is red, most of it is not.
    const int red = redPixels(accented);
    QVERIFY(red > 0);
    QVERIFY(red < litPixels(accented) / 2);

    // A rule that picks nothing leaves the block exactly as it was.
    clip.textStyle.accent.rule = drift::WordAccentRule::None;
    QCOMPARE(redPixels(rasterizeText(clip, rect, 1.0).image), 0);
}

void EngineTest::karaokeAccentFollowsThePlayhead()
{
    SKIP_WITHOUT_FONTS();

    const QRectF rect(0, 0, 700, 200);
    const QString text = QStringLiteral("Number of thumbnails that");
    drift::Clip clip = makeTextClip(text, rect);
    clip.textStyle.fontFamily = QStringLiteral("Inter");
    clip.textStyle.fontWeight = 800;
    clip.textStyle.pixelSize = 48;
    clip.textStyle.accent.rule = drift::WordAccentRule::Karaoke;
    clip.textStyle.accent.colorEnabled = true;
    clip.textStyle.accent.color = QColor(255, 0, 0);

    const QImage first = rasterizeText(clip, text, rect, 1.0, 0).image;
    const QImage third = rasterizeText(clip, text, rect, 1.0, 2).image;
    QVERIFY(!first.isNull());
    QVERIFY(redPixels(first) > 0);
    QVERIFY(redPixels(third) > 0);
    // Different word lit, so genuinely different pixels — not just a different cache slot.
    QVERIFY(first != third);

    // The spoken word still only costs one raster: the same index hits the cache.
    QCOMPARE(rasterizeText(clip, text, rect, 1.0, 0).image.cacheKey(), first.cacheKey());
}

void EngineTest::accentSizeScaleWidensTheBlock()
{
    SKIP_WITHOUT_FONTS();

    const QRectF rect(0, 0, 900, 240);
    drift::Clip clip = makeTextClip(QStringLiteral("Number of thumbnails"), rect);
    clip.textStyle.fontFamily = QStringLiteral("Inter");
    clip.textStyle.fontWeight = 800;
    clip.textStyle.pixelSize = 40;

    const QRect plain = inkBounds(rasterizeText(clip, rect, 1.0).image);
    QVERIFY(plain.isValid());

    clip.textStyle.accent.rule = drift::WordAccentRule::FirstWord;
    clip.textStyle.accent.sizeScale = 2.0;
    const QRect scaled = inkBounds(rasterizeText(clip, rect, 1.0).image);

    // The scaled word takes more room on the line and stands taller than the rest.
    QVERIFY(scaled.width() > plain.width());
    QVERIFY(scaled.height() > plain.height());
}

void EngineTest::everyStylePackRenders()
{
    SKIP_WITHOUT_FONTS();

    // A pack naming a font that is not bundled, or an all-transparent colour combination, would
    // ship a card and a caption that render as nothing at all.
    const QRectF rect(0, 0, 900, 300);
    const QString text = QStringLiteral("Number of thumbnails that");
    for (const drift::TextPreset &preset : drift::textPresets()) {
        drift::Clip clip = makeTextClip(text, rect);
        clip.textStyle = preset.style;
        const int activeWord =
            preset.style.accent.rule == drift::WordAccentRule::Karaoke ? 1 : -1;
        const QImage image = rasterizeText(clip, text, rect, 1.0, activeWord).image;
        QVERIFY2(!image.isNull(), qPrintable(preset.id));
        QVERIFY2(litPixels(image) > 0, qPrintable(preset.id));
    }
}

void EngineTest::heavyWeightsRenderSolidGlyphs()
{
    SKIP_WITHOUT_FONTS();

    // Where two glyph contours overlap — which heavy weights and tight spacing make common —
    // QPainterPath's odd-even default punches the overlap out into a transparent hole.
    // "W" has no counter, so in "WWWW" *any* enclosed transparent region is such a hole.
    const QRectF rect(0, 0, 900, 200);
    drift::Clip clip = makeTextClip(QStringLiteral("WWWW"), rect);
    clip.textStyle.fontFamily = QStringLiteral("Montserrat");
    clip.textStyle.fontWeight = 900;
    clip.textStyle.pixelSize = 80;
    clip.textStyle.letterSpacing = -30.0; // force the glyphs to overlap each other

    const QImage image = rasterizeText(clip, rect, 1.0).image;
    QVERIFY(!image.isNull());

    // Flood the transparent background inward from the border; whatever transparent pixels it
    // cannot reach are enclosed by ink, i.e. holes.
    const int w = image.width();
    const int h = image.height();
    const auto transparent = [&](int x, int y) { return qAlpha(image.pixel(x, y)) < 128; };

    QVector<bool> reached(w * h, false);
    QVector<QPoint> stack;
    for (int x = 0; x < w; ++x) {
        stack.append({x, 0});
        stack.append({x, h - 1});
    }
    for (int y = 0; y < h; ++y) {
        stack.append({0, y});
        stack.append({w - 1, y});
    }
    while (!stack.isEmpty()) {
        const QPoint p = stack.takeLast();
        if (p.x() < 0 || p.y() < 0 || p.x() >= w || p.y() >= h)
            continue;
        const int i = p.y() * w + p.x();
        if (reached[i] || !transparent(p.x(), p.y()))
            continue;
        reached[i] = true;
        stack.append({p.x() + 1, p.y()});
        stack.append({p.x() - 1, p.y()});
        stack.append({p.x(), p.y() + 1});
        stack.append({p.x(), p.y() - 1});
    }

    int enclosed = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (transparent(x, y) && !reached[y * w + x])
                ++enclosed;
        }
    }

    // Winding fill leaves exactly zero here; odd-even leaves hundreds.
    QVERIFY2(enclosed < 20,
             qPrintable(QStringLiteral("%1 enclosed transparent px — overlapping glyph contours "
                                       "are being punched out (fill rule regression)")
                            .arg(enclosed)));
}

void EngineTest::textClipCarriesGpuEffects()
{
    SKIP_WITHOUT_FONTS();

    drift::Project project;
    project.setResolution(128, 128);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Text});

    drift::Clip clip = makeTextClip(QStringLiteral("FX"), QRectF(0, 0, 128, 128));
    clip.textStyle.fontFamily = QStringLiteral("Anton");
    clip.textStyle.pixelSize = 48;
    clip.textStyle.color = QColor(120, 120, 120);
    project.tracks()[0].clips.append(clip);

    FrameCompositor compositor;
    compositor.setProject(&project);
    const QImage plain = compositor.compositeAt(drift::secondsToUs(1.0));
    QVERIFY(!plain.isNull());

    drift::Effect brightness;
    brightness.catalogId = QStringLiteral("adjust.brightness");
    brightness.parameters.insert(QStringLiteral("brightness"), 0.9);
    project.tracks()[0].clips[0].effects.append(brightness);

    const QImage brightened = compositor.compositeAt(drift::secondsToUs(1.0));
    QVERIFY(!brightened.isNull());
    // Effects used to be dropped for text clips: the layer only got them in the video branch.
    QVERIFY2(meanLuminance(brightened) > meanLuminance(plain) + 1.0,
             "GPU effect had no visible effect on a text clip");
}

void EngineTest::textAnimationFadesAndSlides()
{
    SKIP_WITHOUT_FONTS();

    drift::Project project;
    project.setResolution(128, 128);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Text});

    drift::Clip clip = makeTextClip(QStringLiteral("IN"), QRectF(0, 0, 128, 128));
    clip.textStyle.fontFamily = QStringLiteral("Anton");
    clip.textStyle.pixelSize = 48;
    clip.textStyle.animIn = {drift::TextAnimKind::Fade, drift::secondsToUs(1.0), drift::TextEase::Linear};
    project.tracks()[0].clips.append(clip);

    FrameCompositor compositor;
    compositor.setProject(&project);

    const double early = meanLuminance(compositor.compositeAt(drift::secondsToUs(0.05)));
    const double mid = meanLuminance(compositor.compositeAt(drift::secondsToUs(0.5)));
    const double settled = meanLuminance(compositor.compositeAt(drift::secondsToUs(2.0)));
    QVERIFY2(early < mid && mid < settled, "fade-in did not ramp up");

    // Once past the entrance the text holds steady rather than continuing to change.
    const double later = meanLuminance(compositor.compositeAt(drift::secondsToUs(3.0)));
    QVERIFY(qAbs(later - settled) < 0.5);

    // A slide-up entrance arrives from below, so the glyphs start lower than they finish.
    project.tracks()[0].clips[0].textStyle.animIn = {drift::TextAnimKind::SlideUp,
                                                     drift::secondsToUs(1.0), drift::TextEase::Linear};
    const double startY = litCentroidY(compositor.compositeAt(drift::secondsToUs(0.1)));
    const double endY = litCentroidY(compositor.compositeAt(drift::secondsToUs(2.0)));
    QVERIFY2(startY > endY + 1.0, "slide-up entrance did not travel upward");
}

void EngineTest::maskApplierEllipseMasksCorners()
{
    QImage image(64, 64, QImage::Format_RGBA8888);
    image.fill(Qt::white);

    drift::Mask mask;
    mask.shape = drift::MaskShape::Ellipse;
    mask.x = 0.5;
    mask.y = 0.5;
    mask.w = 0.5;
    mask.h = 0.5;

    const QImage masked = drift::applyMask(image, mask, 64, 64);
    QVERIFY(qAlpha(masked.pixel(32, 32)) > 200);
    QVERIFY(qAlpha(masked.pixel(0, 0)) < 20);
}

void EngineTest::exporterProducesPlayableFileWithBackground()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    drift::Project project;
    project.setResolution(160, 90);
    project.setFps(25);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Shape});

    // A small centered shape so the canvas corners show the background.
    drift::Clip clip;
    clip.id = QStringLiteral("shape");
    clip.type = drift::ClipType::Shape;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(1.0);
    clip.shapeStyle.kind = drift::ShapeKind::Rectangle;
    clip.shapeStyle.fill = Qt::red;
    clip.shapeStyle.strokeWidth = 0.0;
    clip.transformX.setKeyframe(0, 70.0);
    clip.transformY.setKeyframe(0, 35.0);
    clip.transformW.setKeyframe(0, 20.0);
    clip.transformH.setKeyframe(0, 20.0);
    project.tracks()[0].clips.append(clip);

    // Non-default background must be baked into the exported frames.
    drift::Background background;
    background.kind = drift::BackgroundKind::Color;
    background.color = QColor(Qt::blue);
    project.setBackground(background);

    ExportSettings settings = Exporter::defaultSettings();
    settings.targetHeight = 0;
    settings.videoCodecId = QStringLiteral("h264");
    settings.audioCodecId = QStringLiteral("aac");
    settings.rateControl = QStringLiteral("crf");
    settings.crf = 23;

    // Prefer an available video codec if h264 is missing.
    if (!Exporter::videoCodecById(settings.videoCodecId).value(QStringLiteral("available")).toBool()) {
        bool found = false;
        for (const QVariant &v : Exporter::videoCodecs()) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("available")).toBool()) {
                settings.videoCodecId = m.value(QStringLiteral("id")).toString();
                found = true;
                break;
            }
        }
        if (!found)
            QSKIP("No video encoder available in this FFmpeg build");
    }
    if (!Exporter::audioCodecById(settings.audioCodecId).value(QStringLiteral("available")).toBool()) {
        bool found = false;
        for (const QVariant &v : Exporter::audioCodecs()) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("available")).toBool()) {
                settings.audioCodecId = m.value(QStringLiteral("id")).toString();
                found = true;
                break;
            }
        }
        if (!found)
            QSKIP("No audio encoder available in this FFmpeg build");
    }

    // The fallback codecs need not be mp4-muxable (an LGPL FFmpeg has no x264 and lands on ffv1),
    // so let the pair pick the container rather than hardcoding one.
    const QString out = dir.filePath(
        QStringLiteral("out.") + Exporter::defaultSuffix(
            Exporter::preferredContainer(settings.videoCodecId, settings.audioCodecId)));

    QString error;
    const bool ok = Exporter::run(project, settings, out, &error);
    if (!ok && error.contains(QStringLiteral("encoder")))
        QSKIP("Selected encoder not available in this FFmpeg build");
    QVERIFY2(ok, qPrintable(error));
    QVERIFY(QFileInfo(out).size() > 0);

    ClipReader reader;
    QVERIFY(reader.open(out));
    QVERIFY(reader.hasVideo());

    QImage frame;
    QVERIFY(reader.readVideoFrameAt(drift::secondsToUs(0.5), frame, 160, 90));
    QVERIFY(!frame.isNull());

    // A corner is background (blue), away from the centered red shape.
    const QRgb corner = frame.pixel(6, 6);
    QVERIFY(qBlue(corner) > 150);
    QVERIFY(qBlue(corner) > qRed(corner));
}

// The audio-effects addon content must parse into a usable catalog: known ids resolve, categories
// are discovered, and every manifest carries a chain. A broken manifest would be skipped silently,
// so assert the expected count rather than merely "non-empty".
namespace {

// Build a rack for `effects` and run the whole buffer through it in one pass. The mixer does this
// block by block; a single pass is the reference those blocks must agree with.
QVector<float> runRack(const QList<drift::Effect> &effects, const float *interleavedStereo,
                       int frames, int sampleRate)
{
    QVector<float> out(frames * 2, 0.0f);
    if (interleavedStereo)
        std::memcpy(out.data(), interleavedStereo, static_cast<size_t>(frames) * 2 * sizeof(float));

    drift::AudioEffectRack rack;
    if (rack.configure(audioEffectSpecsFor(effects), sampleRate))
        rack.process(out.data(), frames);
    return out;
}

QVector<float> stereoTone(int frames, double hz, int sampleRate, float amplitude = 1.0f)
{
    QVector<float> tone(frames * 2);
    for (int i = 0; i < frames; ++i) {
        const auto s = static_cast<float>(amplitude * std::sin(2.0 * M_PI * hz * i / sampleRate));
        tone[i * 2] = s;
        tone[i * 2 + 1] = s;
    }
    return tone;
}

double rms(const float *samples, int count)
{
    double sum = 0.0;
    for (int i = 0; i < count; ++i)
        sum += static_cast<double>(samples[i]) * samples[i];
    return std::sqrt(sum / std::max(1, count));
}
// Naive DFT at one frequency. Enough to ask "is the energy where it should be".
double toneEnergy(const float *interleaved, int frames, double hz, int rate)
{
    const double w = 2.0 * M_PI * hz / rate;
    double re = 0.0;
    double im = 0.0;
    for (int i = 0; i < frames; ++i) {
        re += interleaved[i * 2] * std::cos(w * i);
        im += interleaved[i * 2] * std::sin(w * i);
    }
    return std::sqrt(re * re + im * im) / frames;
}

} // namespace

// A single sample dropped once per mix block is a periodic impulse: a buzz at rate/block with
// harmonics all the way to Nyquist, which is what it looks like on a spectrogram. It came from
// deriving the source frame count through microseconds — 1024 frames at 48 kHz is 21333.33 us, and
// truncating into µs and back out again asks for 1023 frames to fill 1024, leaving the last one at
// the buffer's initial zero. Only block sizes lasting a whole number of µs escaped it, and audio
// device periods are powers of two.
void EngineTest::mixerHasNoBlockBoundaryDropout()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = makeToneAudio(dir);
    if (path.isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");

    constexpr int kRate = 48000;
    constexpr double kToneHz = 440.0;
    constexpr drift::TimeUs kDurationUs = 1'500'000;

    drift::Project project;
    project.setSampleRate(kRate);
    drift::Track track{.type = drift::TrackType::Audio};
    drift::Clip clip;
    clip.id = QStringLiteral("tone");
    clip.type = drift::ClipType::Audio;
    clip.path = path;
    clip.timelineStart = 0;
    clip.timelineDuration = kDurationUs;
    clip.srcIn = 0;
    clip.srcOut = kDurationUs;
    track.clips.append(clip);
    project.tracks().append(track);

    AudioMixer mixer;
    mixer.setProject(&project);

    const int total = static_cast<int>((kDurationUs * kRate) / drift::kUsPerSecond);

    // 480 lasts exactly 10000 us and always worked; the rest do not divide evenly and did not.
    for (const int block : {1024, 512, 1000, 480}) {
        QVector<float> out(total * 2, 0.0f);
        for (int offset = 0; offset < total; offset += block) {
            const int count = std::min(block, total - offset);
            const auto startUs =
                static_cast<drift::TimeUs>((static_cast<int64_t>(offset) * drift::kUsPerSecond) / kRate);
            mixer.mix(startUs, count, kRate, out.data() + static_cast<size_t>(offset) * 2);
        }

        const int skip = kRate / 10; // let the decoder settle
        float amplitude = 0.0f;
        for (int i = skip; i < total; ++i)
            amplitude = std::max(amplitude, std::abs(out[i * 2]));
        QVERIFY2(amplitude > 0.01f, "mixed tone is silent");

        // A band-limited sine cannot step by more than this between adjacent samples. Anything
        // beyond it is a splice, not signal.
        const auto bound = static_cast<float>(amplitude * 2.0 * std::sin(M_PI * kToneHz / kRate));

        float worst = 0.0f;
        int worstIndex = 0;
        for (int i = skip + 1; i < total; ++i) {
            const float delta = std::abs(out[i * 2] - out[(i - 1) * 2]);
            if (delta > worst) {
                worst = delta;
                worstIndex = i;
            }
        }

        QVERIFY2(worst < bound * 1.5f,
                 qPrintable(QStringLiteral("block=%1: step %2 at frame %3 (phase %4) exceeds the %5 "
                                           "a %6 Hz tone can produce")
                                .arg(block).arg(worst).arg(worstIndex)
                                .arg(worstIndex % block).arg(bound).arg(kToneHz)));
    }
}

// PlaybackEngine clears the effect racks from the GUI thread on every seek, play and pause, while
// mix() is running on the audio thread. Taking a reference into the hash instead of a strong
// reference — and touching the hash at all without a lock — segfaults inside the rack's buffers
// once the timing lines up, which is what "crashed after a while" looks like from the outside.
void EngineTest::mixerSurvivesConcurrentEffectRackReset()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = makeToneAudio(dir);
    if (path.isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");

    constexpr int kRate = 48000;
    constexpr drift::TimeUs kDurationUs = 2'000'000;

    drift::Project project;
    project.setSampleRate(kRate);
    drift::Track track{.type = drift::TrackType::Audio};
    drift::Clip clip;
    clip.id = QStringLiteral("tone");
    clip.type = drift::ClipType::Audio;
    clip.path = path;
    clip.timelineStart = 0;
    clip.timelineDuration = kDurationUs;
    clip.srcIn = 0;
    clip.srcOut = kDurationUs;
    drift::Effect echo;
    echo.catalogId = QStringLiteral("space.echo");
    clip.audioEffects.append(echo);
    track.clips.append(clip);
    project.tracks().append(track);

    AudioMixer mixer;
    mixer.setProject(&project);

    std::atomic<bool> stop{false};
    QScopedPointer<QThread> mixThread(QThread::create([&mixer, &stop] {
        QVector<float> buffer(1024 * 2);
        drift::TimeUs t = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            mixer.mix(t, 1024, kRate, buffer.data());
            t = (t + 21333) % 1'500'000;
        }
    }));
    QScopedPointer<QThread> resetThread(QThread::create([&mixer, &stop] {
        while (!stop.load(std::memory_order_relaxed))
            mixer.resetEffectRacks();
    }));

    mixThread->start();
    resetThread->start();
    QThread::msleep(2000);
    stop.store(true);
    QVERIFY(mixThread->wait(10000));
    QVERIFY(resetThread->wait(10000));
}

void EngineTest::audioEffectCatalogLoadsPackages()
{
    const QList<AudioEffectEntry> &catalog = audioEffectCatalog();
    QVERIFY2(catalog.size() >= 20,
             qPrintable(QStringLiteral("only %1 audio effects loaded").arg(catalog.size())));

    const AudioEffectEntry *telephone = audioEffectDefForId(QStringLiteral("transmission.telephone"));
    QVERIFY(telephone);
    QCOMPARE(telephone->displayName, QStringLiteral("Telephone"));
    QCOMPARE(telephone->category, QStringLiteral("transmission"));
    QCOMPARE(telephone->processorId, QStringLiteral("bandlimit"));
    QCOMPARE(telephone->icon, QStringLiteral("phone"));
    QVERIFY2(!telephone->thumbnailPath.isEmpty(), "telephone package should ship thumbnail.png");
    QVERIFY(QFileInfo::exists(telephone->thumbnailPath));

    const AudioEffectEntry *chipmunk = audioEffectDefForId(QStringLiteral("voice.chipmunk"));
    QVERIFY(chipmunk);
    QCOMPARE(chipmunk->processorId, QStringLiteral("pitch"));
    QCOMPARE(chipmunk->parameters.size(), 1);
    QCOMPARE(chipmunk->parameters[0].key, QStringLiteral("pitch"));

    const QList<QPair<QString, QString>> categories = audioEffectCategories();
    QSet<QString> slugs;
    for (const auto &c : categories)
        slugs.insert(c.first);
    QVERIFY(slugs.contains(QStringLiteral("voice")));
    QVERIFY(slugs.contains(QStringLiteral("transmission")));

    // Every catalog entry must name a processor, or the mixer has nothing to run.
    for (const AudioEffectEntry &entry : catalog)
        QVERIFY2(!entry.processorId.isEmpty(), qPrintable(entry.id));
}

void EngineTest::audioEffectFactoryBuildsEveryCatalogEntry()
{
    // A manifest naming a processor nobody implements used to be discoverable only by hearing
    // nothing. The catalog rejects those at load, so every entry that survived must build.
    const QList<AudioEffectEntry> &catalog = audioEffectCatalog();
    QVERIFY(!catalog.isEmpty());

    for (const AudioEffectEntry &entry : catalog) {
        QVERIFY2(drift::audiofx::hasProcessor(entry.processorId),
                 qPrintable(QStringLiteral("%1 -> %2").arg(entry.id, entry.processorId)));

        // configure() only reports true once the factory has actually built a chain, so this is
        // what proves the processor exists rather than the effect quietly becoming a passthrough.
        // Run it at every rate the mixer uses: 8 kHz for the subtitle waveform, 22050 for beat
        // detection, 48 kHz for playback and export.
        drift::Effect effect;
        effect.catalogId = entry.id;
        const QVector<drift::AudioEffectSpec> specs = audioEffectSpecsFor({effect});
        QCOMPARE(specs.size(), 1);

        for (const int rate : {8000, 22050, 48000}) {
            constexpr int kFrames = 512;
            QVector<float> buffer = stereoTone(kFrames, 440.0, rate, 0.5f);

            drift::AudioEffectRack rack;
            QVERIFY2(rack.configure(specs, rate), qPrintable(entry.id));
            rack.process(buffer.data(), kFrames);

            QCOMPARE(buffer.size(), kFrames * 2);
            for (int i = 0; i < buffer.size(); ++i) {
                QVERIFY2(std::isfinite(buffer[i]),
                         qPrintable(QStringLiteral("%1 @%2Hz produced a non-finite sample")
                                        .arg(entry.id).arg(rate)));
            }
        }
    }
}

void EngineTest::audioEffectChainAltersSignal()
{
    // A 4 kHz tone pushed through the telephone band-limit (300-3400 Hz) must come back quieter,
    // finite, and the right length — a real filter pass, not a passthrough.
    constexpr int kRate = 48000;
    constexpr int kFrames = 4096;
    const QVector<float> tone = stereoTone(kFrames, 4000.0, kRate);
    const double inRms = rms(tone.constData(), tone.size());

    drift::Effect telephone;
    telephone.catalogId = QStringLiteral("transmission.telephone");
    const QVector<float> out = runRack({telephone}, tone.constData(), kFrames, kRate);

    QCOMPARE(out.size(), kFrames * 2);
    for (float s : out)
        QVERIFY(std::isfinite(s));
    const double outRms = rms(out.constData(), out.size());

    // 4 kHz sits above the 3400 Hz cutoff, so the band-limited output is markedly attenuated.
    QVERIFY2(outRms < inRms * 0.6,
             qPrintable(QStringLiteral("in=%1 out=%2").arg(inRms).arg(outRms)));
    QVERIFY2(outRms > 1e-4, "output is silent — the rack likely failed to build");
}

void EngineTest::audioEffectChainBypassesUnknownEffect()
{
    // An effect whose id is not in the catalog (e.g. an addon the user hasn't installed) must be a
    // clean passthrough, never a dropout.
    constexpr int kRate = 48000;
    constexpr int kFrames = 1024;
    const QVector<float> tone = stereoTone(kFrames, 440.0, kRate);

    drift::Effect unknown;
    unknown.catalogId = QStringLiteral("does.not.exist");
    const QVector<float> out = runRack({unknown}, tone.constData(), kFrames, kRate);

    QCOMPARE(out.size(), kFrames * 2);
    for (int i = 0; i < out.size(); ++i)
        QCOMPARE(out[i], tone[i]);
}

void EngineTest::audioEffectStreamIsContinuousAcrossBlocks()
{
    // Tremolo's LFO phase must carry across blocks; resetting every buffer causes audible jitter.
    constexpr int kRate = 48000;
    constexpr int kBlock = 1024;
    constexpr int kBlocks = 8;
    constexpr int kTotal = kBlock * kBlocks;

    const QVector<float> tone = stereoTone(kTotal, 440.0, kRate);

    drift::Effect tremolo;
    tremolo.catalogId = QStringLiteral("space.tremolo");
    tremolo.parameters.insert(QStringLiteral("rate"), 8.0);
    tremolo.parameters.insert(QStringLiteral("depth"), 0.9);

    const QVector<drift::AudioEffectSpec> specs = audioEffectSpecsFor({tremolo});
    drift::AudioEffectRack rack;
    QVERIFY(rack.configure(specs, kRate));

    QVector<float> streamed(kTotal * 2);
    std::memcpy(streamed.data(), tone.constData(), static_cast<size_t>(kTotal) * 2 * sizeof(float));
    for (int block = 0; block < kBlocks; ++block)
        rack.process(streamed.data() + block * kBlock * 2, kBlock);

    const QVector<float> reference = runRack({tremolo}, tone.constData(), kTotal, kRate);
    QCOMPARE(reference.size(), streamed.size());

    // Block-by-block must be bit-comparable to one pass: the sub-block loop inside the rack means
    // the caller's block size cannot change the result.
    double maxDiff = 0.0;
    for (int i = 0; i < streamed.size(); ++i)
        maxDiff = std::max(maxDiff, static_cast<double>(std::abs(streamed[i] - reference[i])));
    QVERIFY2(maxDiff < 1e-6,
             qPrintable(QStringLiteral("block boundary discontinuity maxDiff=%1").arg(maxDiff)));
}

void EngineTest::audioEffectFlangerProcessesSignal()
{
    constexpr int kRate = 48000;
    constexpr int kFrames = 4096;
    const QVector<float> tone = stereoTone(kFrames, 440.0, kRate);

    const AudioEffectEntry *flanger = audioEffectDefForId(QStringLiteral("space.flanger"));
    QVERIFY(flanger);
    QCOMPARE(flanger->parameters.size(), 7);
    QCOMPARE(flanger->processorId, QStringLiteral("flanger"));

    drift::Effect effect;
    effect.catalogId = flanger->id;
    effect.parameters.insert(QStringLiteral("rate"), 0.8);
    effect.parameters.insert(QStringLiteral("phase"), 180.0);
    effect.parameters.insert(QStringLiteral("mix"), 80.0);
    effect.parameters.insert(QStringLiteral("invert"), 1.0);

    const QVector<float> out = runRack({effect}, tone.constData(), kFrames, kRate);
    QCOMPARE(out.size(), kFrames * 2);

    int changed = 0;
    for (int i = 0; i < kFrames * 2; ++i) {
        QVERIFY(std::isfinite(out[i]));
        if (std::abs(out[i] - tone[i]) > 1e-4)
            ++changed;
    }
    const double inRms = rms(tone.constData(), kFrames * 2);
    const double outRms = rms(out.constData(), kFrames * 2);

    QVERIFY2(changed > kFrames, "flanger output matches input — the rack likely failed");
    QVERIFY2(outRms > 1e-4, "flanger output is silent");
    QVERIFY2(outRms < inRms * 2.0,
             qPrintable(QStringLiteral("flanger blew up: in=%1 out=%2").arg(inRms).arg(outRms)));
}

void EngineTest::audioEffectRackPrimingAlignsLatentStages()
{
    // The pitch shifter reads out of a delay line, so it has real latency. The libavfilter path
    // zero-filled what the graph had not produced yet, which is why a pitch-shifted clip opened
    // with silence and then stayed offset. Priming on the audio that precedes the block is the fix.
    constexpr int kRate = 48000;
    constexpr int kBlock = 2048;

    drift::Effect chipmunk;
    chipmunk.catalogId = QStringLiteral("voice.chipmunk");
    chipmunk.parameters.insert(QStringLiteral("pitch"), 1.0);

    const QVector<drift::AudioEffectSpec> specs = audioEffectSpecsFor({chipmunk});
    drift::AudioEffectRack primed;
    QVERIFY(primed.configure(specs, kRate));

    const int primeFrames = primed.primeFrames();
    QVERIFY2(primeFrames > 0, "a latent stage must ask for priming");

    const QVector<float> continuous = stereoTone(primeFrames + kBlock, 440.0, kRate);

    primed.warmUp(continuous.constData(), primeFrames);
    QVector<float> primedOut(kBlock * 2);
    std::memcpy(primedOut.data(), continuous.constData() + primeFrames * 2,
                static_cast<size_t>(kBlock) * 2 * sizeof(float));
    primed.process(primedOut.data(), kBlock);

    drift::AudioEffectRack cold;
    QVERIFY(cold.configure(specs, kRate));
    QVector<float> coldOut(kBlock * 2);
    std::memcpy(coldOut.data(), continuous.constData() + primeFrames * 2,
                static_cast<size_t>(kBlock) * 2 * sizeof(float));
    cold.process(coldOut.data(), kBlock);

    // The opening of the block is the part latency eats. Primed, it carries signal; cold, it is
    // the silence users heard at the head of every pitch-shifted clip.
    constexpr int kHead = 512;
    const double primedHead = rms(primedOut.constData(), kHead * 2);
    const double coldHead = rms(coldOut.constData(), kHead * 2);

    QVERIFY2(primedHead > 0.1,
             qPrintable(QStringLiteral("primed head is quiet: %1").arg(primedHead)));
    QVERIFY2(primedHead > coldHead * 4.0,
             qPrintable(QStringLiteral("priming changed nothing: primed=%1 cold=%2")
                            .arg(primedHead).arg(coldHead)));
}

// A pitch shifter has exactly one job and "the output is finite" does not check it. Chipmunk must
// raise the pitch and Deep Voice must lower it, by the ratio the manifest asks for.
//
// The granular shifter reads two taps out of one delay line. juce's popSample only advances the
// read pointer when told to, and leaving it frozen for both taps pinned the read position while
// the write position kept moving: the traversal rate collapsed from `ratio` to `ratio - 1`, so
// 1.5 came out an octave down and 0.7 came out reversed.
void EngineTest::pitchShiftMovesPitchInTheRightDirection()
{
    constexpr int kRate = 48000;
    constexpr double kToneHz = 440.0;
    constexpr int kMeasure = 24000; // half a second is plenty of resolution

    struct Case
    {
        const char *id;
        double ratio;
    };

    for (const Case &testCase : {Case{"voice.chipmunk", 1.5}, Case{"voice.deep", 0.7}}) {
        drift::Effect effect;
        effect.catalogId = QString::fromLatin1(testCase.id);
        effect.parameters.insert(QStringLiteral("pitch"), testCase.ratio);

        const QVector<drift::AudioEffectSpec> specs = audioEffectSpecsFor({effect});
        QCOMPARE(specs.size(), 1);

        drift::AudioEffectRack rack;
        QVERIFY(rack.configure(specs, kRate));

        const int prime = rack.primeFrames();
        const QVector<float> tone = stereoTone(prime + kMeasure, kToneHz, kRate);
        rack.warmUp(tone.constData(), prime);

        QVector<float> out(kMeasure * 2);
        std::memcpy(out.data(), tone.constData() + prime * 2,
                    static_cast<size_t>(kMeasure) * 2 * sizeof(float));
        rack.process(out.data(), kMeasure);

        const double shifted = kToneHz * testCase.ratio;
        const double atShifted = toneEnergy(out.constData(), kMeasure, shifted, kRate);
        const double atOriginal = toneEnergy(out.constData(), kMeasure, kToneHz, kRate);
        // Where the frozen read pointer used to put it.
        const double atBroken = toneEnergy(out.constData(), kMeasure,
                                           std::abs(kToneHz * (testCase.ratio - 1.0)), kRate);

        QVERIFY2(atShifted > atOriginal * 4.0,
                 qPrintable(QStringLiteral("%1: energy at the shifted %2 Hz (%3) does not dominate "
                                           "the unshifted %4 Hz (%5)")
                                .arg(testCase.id).arg(shifted).arg(atShifted).arg(kToneHz).arg(atOriginal)));
        QVERIFY2(atShifted > atBroken * 4.0,
                 qPrintable(QStringLiteral("%1: energy at %2 Hz (%3) does not dominate the "
                                           "ratio-minus-one artefact at %4 Hz (%5)")
                                .arg(testCase.id).arg(shifted).arg(atShifted)
                                .arg(std::abs(kToneHz * (testCase.ratio - 1.0))).arg(atBroken)));

        // And it must still be a tone, not a smear: the shifted partial should carry real level.
        QVERIFY2(atShifted > 0.02,
                 qPrintable(QStringLiteral("%1: shifted tone is weak (%2)").arg(testCase.id).arg(atShifted)));
    }
}

void EngineTest::audioEffectRackParameterChangeIsContinuous()
{
    // The libavfilter graph rebuilt itself whenever any value changed, so every slider tick
    // restarted the DSP from zero — the click users heard while dragging. Values now ramp.
    constexpr int kRate = 48000;
    constexpr int kBlock = 4096;

    // Constant input: any jump in the output is the parameter, not the signal.
    QVector<float> first(kBlock * 2, 0.5f);
    QVector<float> second(kBlock * 2, 0.5f);

    drift::Effect muffled;
    muffled.catalogId = QStringLiteral("transmission.muffled");
    muffled.parameters.insert(QStringLiteral("cutoff"), 4000.0);
    muffled.parameters.insert(QStringLiteral("gain"), 0.5);

    drift::AudioEffectRack rack;
    QVERIFY(rack.configure(audioEffectSpecsFor({muffled}), kRate));
    rack.process(first.data(), kBlock);

    muffled.parameters.insert(QStringLiteral("gain"), 2.0);
    QVERIFY(rack.configure(audioEffectSpecsFor({muffled}), kRate));
    rack.process(second.data(), kBlock);

    const float boundaryStep = std::abs(second[0] - first[(kBlock - 1) * 2]);
    // An unsmoothed 0.5 -> 2.0 gain change on a 0.5 input steps by 0.75 in one sample.
    QVERIFY2(boundaryStep < 0.05f,
             qPrintable(QStringLiteral("parameter change stepped by %1").arg(boundaryStep)));

    // It must still actually arrive at the new value.
    const float settled = second[(kBlock - 1) * 2];
    QVERIFY2(std::abs(settled - 1.0f) < 0.05f,
             qPrintable(QStringLiteral("gain never reached its target: %1").arg(settled)));
}

// ---- DeepFilterNet3 denoiser -------------------------------------------------------------
//
// The model directory is gitignored, so every case here skips when it is absent. Point
// DRIFT_DENOISE_MODEL_DIR elsewhere to test an installed addon instead.
namespace {

bool denoiseModelAvailable()
{
    const QString dir = QString::fromUtf8(DRIFT_TEST_DENOISE_MODEL_DIR);
    if (QDir(dir).exists())
        qputenv("DRIFT_DENOISE_MODEL_DIR", dir.toUtf8());
    return drift::DeepFilterDenoiser::modelPresent();
}

double rms(const std::vector<float> &x, size_t from = 0, size_t to = SIZE_MAX)
{
    const size_t end = std::min(to, x.size());
    if (end <= from)
        return 0.0;
    double acc = 0.0;
    for (size_t i = from; i < end; ++i)
        acc += double(x[i]) * x[i];
    return std::sqrt(acc / double(end - from));
}

std::vector<float> whiteNoise(int samples, float amplitude, uint32_t seed)
{
    std::mt19937 rng(seed);
    std::normal_distribution<float> gauss(0.0f, amplitude);
    std::vector<float> out(size_t(samples), 0.0f);
    for (int i = 0; i < samples; ++i)
        out[size_t(i)] = gauss(rng);
    return out;
}

} // namespace

// The auxiliary blob is the model's own ERB geometry. If the forward and inverse matrices do not
// describe the same 32 bands, every gain lands on the wrong frequencies and the result is
// plausible-sounding rubbish rather than an obvious failure.
void EngineTest::denoiseAuxiliaryConstantsRoundTrip()
{
    const QString path =
        QDir(QString::fromUtf8(DRIFT_TEST_DENOISE_MODEL_DIR)).filePath(QStringLiteral("deepfilter-auxiliary.bin"));
    if (!QFile::exists(path))
        QSKIP("DeepFilterNet3 model not installed");

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    constexpr int kBins = 481;
    constexpr int kBands = 32;
    constexpr int kFft = 960;
    QCOMPARE(f.size(), qint64(kBins * kBands + kBands * kBins + kFft) * 4);

    const QByteArray blob = f.readAll();
    const auto *fwd = reinterpret_cast<const float *>(blob.constData());
    const float *inv = fwd + kBins * kBands;
    const float *win = inv + kBands * kBins;

    // Every bin belongs to exactly one band, in both directions.
    for (int k = 0; k < kBins; ++k) {
        int owners = 0;
        for (int b = 0; b < kBands; ++b) {
            if (fwd[k * kBands + b] != 0.0f)
                ++owners;
            QCOMPARE(inv[b * kBins + k] != 0.0f, fwd[k * kBands + b] != 0.0f);
        }
        QCOMPARE(owners, 1);
    }

    // The forward weights are 1/bandwidth, so a flat unit spectrum must average to 1 per band.
    for (int b = 0; b < kBands; ++b) {
        double acc = 0.0;
        for (int k = 0; k < kBins; ++k)
            acc += fwd[k * kBands + b];
        QVERIFY2(std::abs(acc - 1.0) < 1e-5,
                 qPrintable(QStringLiteral("band %1 forward weights sum to %2").arg(b).arg(acc)));
    }

    // Vorbis window: zero at the edges, unity at the centre, and Princen-Bradley (w^2 sums to 1
    // across a 50% overlap) — the property the overlap-add synthesis relies on.
    QCOMPARE(win[0], 0.0f);
    for (int n = 0; n < kFft / 2; ++n) {
        const double a = win[n];
        const double b = win[n + kFft / 2];
        QVERIFY2(std::abs(a * a + b * b - 1.0) < 1e-4,
                 qPrintable(QStringLiteral("window fails Princen-Bradley at %1").arg(n)));
    }
}

// Length must survive exactly (the clip on the timeline depends on it), and digital silence must
// come back as silence rather than as the model's idea of a noise floor.
void EngineTest::denoisePreservesLengthAndSilence()
{
    if (!denoiseModelAvailable())
        QSKIP("DeepFilterNet3 model not installed");

    drift::DeepFilterDenoiser &dn = drift::DeepFilterDenoiser::instance();
    if (!dn.available())
        QSKIP(qPrintable(dn.lastError()));

    const int rate = drift::DeepFilterDenoiser::sampleRate();
    const std::vector<float> silence(size_t(rate) * 2, 0.0f);
    const std::vector<float> out = dn.denoise(silence, {});

    QCOMPARE(out.size(), silence.size());
    QVERIFY2(rms(out) < 1e-6, qPrintable(QStringLiteral("silence came back at %1").arg(rms(out))));

    // An odd, non-frame-aligned length must round-trip too.
    const std::vector<float> odd(size_t(rate) + 137, 0.0f);
    QCOMPARE(dn.denoise(odd, {}).size(), odd.size());
}

// The point of the feature. Speech-free broadband noise is the one input whose correct handling
// can be asserted without shipping an audio fixture: the model must recognise that none of it is
// speech and pull it a long way down.
//
// Note that a synthesised "voice" (a harmonic stack, say) is NOT a useful test signal here — the
// model correctly declines to treat it as speech and suppresses it too, so a test built on one
// measures nothing. The end-to-end quality check is SI-SDR against a real reference recording;
// see the plan's verification notes.
void EngineTest::denoiseRemovesBroadbandNoise()
{
    if (!denoiseModelAvailable())
        QSKIP("DeepFilterNet3 model not installed");

    drift::DeepFilterDenoiser &dn = drift::DeepFilterDenoiser::instance();
    if (!dn.available())
        QSKIP(qPrintable(dn.lastError()));

    const int rate = drift::DeepFilterDenoiser::sampleRate();
    const std::vector<float> noise = whiteNoise(rate * 4, 0.1f, 1234);

    const std::vector<float> out = dn.denoise(noise, {});
    QCOMPARE(out.size(), noise.size());
    for (float s : out)
        QVERIFY(std::isfinite(s));

    // Skip the first half second: the model is still settling into the signal there.
    const double in = rms(noise, size_t(rate) / 2);
    const double got = rms(out, size_t(rate) / 2);
    QVERIFY2(got < in * 0.25,
             qPrintable(QStringLiteral("noise only fell from %1 to %2").arg(in).arg(got)));
}

// Audio longer than one inference window is stitched from several ONNX runs. The run-up frames and
// the carried normalisation state exist so those joins are inaudible; this is what catches it if
// they regress. A window that started cold would need time to recognise the noise, so the samples
// just after the boundary would come through markedly louder than those just before it.
void EngineTest::denoiseHasNoSeamAcrossWindows()
{
    if (!denoiseModelAvailable())
        QSKIP("DeepFilterNet3 model not installed");

    drift::DeepFilterDenoiser &dn = drift::DeepFilterDenoiser::instance();
    if (!dn.available())
        QSKIP(qPrintable(dn.lastError()));

    const int rate = drift::DeepFilterDenoiser::sampleRate();
    // 25 s crosses the 20 s window boundary once, with room either side of the join.
    const std::vector<float> noise = whiteNoise(rate * 25, 0.1f, 99);
    const std::vector<float> out = dn.denoise(noise, {});
    QCOMPARE(out.size(), noise.size());
    for (float s : out)
        QVERIFY(std::isfinite(s));

    const size_t seam = size_t(rate) * 20;
    const size_t win = size_t(rate) / 5; // 200 ms
    const double before = rms(out, seam - win, seam);
    const double after = rms(out, seam, seam + win);
    const double ratio = after / std::max(before, 1e-12);
    QVERIFY2(ratio > 0.4 && ratio < 2.5,
             qPrintable(QStringLiteral("energy steps across the window seam: %1 -> %2 (x%3)")
                            .arg(before)
                            .arg(after)
                            .arg(ratio)));
}

// The denoised render is only useful if the rest of the app can read it back as ordinary media.
void EngineTest::audioFileWriterRoundTripsThroughClipReader()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("denoised.flac"));

    constexpr int kRate = 48000;
    constexpr int kFrames = kRate; // 1 s
    std::vector<float> tone(size_t(kFrames) * 2);
    for (int i = 0; i < kFrames; ++i) {
        const float s = 0.5f * std::sin(2.0 * M_PI * 440.0 * i / kRate);
        tone[size_t(i) * 2] = s;
        tone[size_t(i) * 2 + 1] = s;
    }

    QString error;
    drift::AudioFileWriter writer;
    QVERIFY2(writer.open(path, kRate, 2, &error), qPrintable(error));
    // Deliberately not a multiple of the encoder frame size, to exercise the partial-frame buffer.
    QVERIFY2(writer.writeFrames(tone.data(), 1000, &error), qPrintable(error));
    QVERIFY2(writer.writeFrames(tone.data() + 2000, kFrames - 1000, &error), qPrintable(error));
    QVERIFY2(writer.finish(&error), qPrintable(error));
    QVERIFY(QFileInfo::exists(path));
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".part")));

    std::vector<float> read(size_t(kFrames) * 2, 0.0f);
    const int got = ClipReaderPool::instance().readAudioInterleaved(path, 0, kFrames, kRate,
                                                                    read.data());
    QVERIFY2(got > kFrames / 2, qPrintable(QStringLiteral("decoded only %1 frames").arg(got)));

    double acc = 0.0;
    for (int i = 0; i < got * 2; ++i)
        acc += double(read[size_t(i)]) * read[size_t(i)];
    const double outRms = std::sqrt(acc / (got * 2));
    // 0.5 amplitude sine -> 0.3536 RMS. FLAC is lossless, so this is tight.
    QVERIFY2(std::abs(outRms - 0.3536) < 0.02,
             qPrintable(QStringLiteral("round-tripped RMS %1").arg(outRms)));
}

void EngineTest::onsetsDetectClickTrackTempo()
{
    constexpr int kRate = 22050;
    constexpr double kPeriod = 0.5; // 120 BPM
    constexpr int kClicks = 20;
    constexpr int kFrames = int(kRate * kPeriod * kClicks);

    // Exponentially decaying noise bursts — broadband, so every FFT bin jumps at once.
    std::vector<float> pcm(kFrames, 0.0f);
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    for (int c = 0; c < kClicks; ++c) {
        const int at = int(c * kPeriod * kRate);
        for (int i = 0; i < kRate / 20 && at + i < kFrames; ++i)
            pcm[size_t(at + i)] = noise(rng) * std::exp(-i / (kRate * 0.01f));
    }

    const AudioBeatAnalysis a = AudioOnsets::analyze(pcm.data(), kFrames, kRate, 0.0);

    QVERIFY2(std::abs(a.bpm - 120.0) < 2.0,
             qPrintable(QStringLiteral("bpm %1").arg(a.bpm)));
    QVERIFY2(a.confidence > 0.5, qPrintable(QStringLiteral("confidence %1").arg(a.confidence)));
    // Including the click at sample 0 — flux only sees it because analyze() pads the front.
    QCOMPARE(a.onsets.size(), kClicks);
    for (int i = 0; i < a.onsets.size(); ++i) {
        const double expected = i * kPeriod;
        QVERIFY2(std::abs(a.onsets[i].seconds - expected) < 0.025,
                 qPrintable(QStringLiteral("onset %1 at %2, expected %3")
                                .arg(i).arg(a.onsets[i].seconds).arg(expected)));
    }

    // The grid must line up with the clicks, not merely have the right spacing.
    QVERIFY(!a.beats.isEmpty());
    for (double b : a.beats) {
        const double offset = std::fmod(b + kPeriod / 2, kPeriod) - kPeriod / 2;
        QVERIFY2(std::abs(offset) < 0.03, qPrintable(QStringLiteral("beat at %1").arg(b)));
    }

    // Times are absolute: the same PCM offset into the timeline shifts everything.
    const AudioBeatAnalysis shifted = AudioOnsets::analyze(pcm.data(), kFrames, kRate, 7.5);
    QVERIFY(std::abs(shifted.onsets.first().seconds - 7.5) < 0.025);
}

void EngineTest::onsetsIgnoreSilence()
{
    constexpr int kRate = 22050;
    const std::vector<float> silence(kRate * 5, 0.0f);

    const AudioBeatAnalysis a = AudioOnsets::analyze(silence.data(), int(silence.size()), kRate, 0.0);
    QVERIFY(a.onsets.isEmpty());
    QCOMPARE(a.bpm, 0.0);
    QVERIFY(a.beats.isEmpty());

    // Too short to say anything about tempo, even with content.
    std::vector<float> blip(kRate, 0.0f);
    for (int i = 0; i < kRate / 40; ++i)
        blip[size_t(i + 1000)] = 0.8f;
    const AudioBeatAnalysis b = AudioOnsets::analyze(blip.data(), int(blip.size()), kRate, 0.0);
    QCOMPARE(b.bpm, 0.0);
}

QTEST_MAIN(EngineTest)
#include "tst_engine.moc"
