#include <QtTest>

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include <QScopeGuard>

#include "models/AppController.h"
#include "models/AssetLibrary.h"

#include "core/Clip.h"
#include "core/Project.h"
#include "core/Track.h"

class EditorStateTest : public QObject
{
    Q_OBJECT

private slots:
    void snapTimeEnabled();
    void addTextClip();
    void addTextClipEmptyUsesPlaceholder();
    void addTextClipWithTextDoesNotRequestEdit();
    void undoRedoClipAdd();
    void undoTrackMute();
    void packagedProjectCarriesDerivedArtifacts();
    void undoBookmarkAdd();
    void moveTrackReordersAndRemapsSelection();
    void addTrackInsertsEmptyTrackByType();
    void projectPersistenceRoundTrip();
    void textStyleBlendModeKeyframesAndEffects();
    void previewSetTextRectScalesPixelSize();
    void fontCatalogIsExposedToQml();
    void effectBrowserCategoriesAndApply();
    void multiSelectClipboardGuidesAndShortcuts();
    void addTransitionBetweenAdjacentClips();
    void setTransitionKindAndDurationPersist();
    void replaceTransitionOnDrop();
    void overlapAutoAppliesCrossfade();
    void separateAudioFromCombinedClip();
    void linkedAudioUnlinkAndMove();
    void keyframeGraphPropertySelection();
    void keyframesCanBeDisabledPerProperty();
    void effectParamKeyframes();
    void effectRemovalRemapsGraphSelection();
    void denoiseAddsCleanedClipOnTrackAbove();
    void shapeStylePartialUpdateAndUndo();
};

void EditorStateTest::snapTimeEnabled()
{
    AssetLibrary library;
    AppController state(&library);
    state.setSnapEnabled(true);
    QCOMPARE(state.snapTime(0.0), 0.0);
    QVERIFY(state.snapTime(1.234) >= 0.0);
}

void EditorStateTest::addTextClip()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Hello"), 0.0);
    QVERIFY(state.durationSeconds() > 0.0);
    QCOMPARE(state.selectedClip(), 0);
}

// Adding with no text drops in a placeholder clip and asks the preview to open
// its inline editor, instead of the click doing nothing at all.
void EditorStateTest::addTextClipEmptyUsesPlaceholder()
{
    AssetLibrary library;
    AppController state(&library);
    QSignalSpy spy(&state, &AppController::inlineTextEditRequested);

    state.addTextClip(QString(), 0.0);

    QCOMPARE(spy.count(), 1);
    const int trackIndex = spy.at(0).at(0).toInt();
    const int clipIndex = spy.at(0).at(1).toInt();
    QCOMPARE(clipIndex, state.selectedClip());

    const QVariantMap clip = state.clipAt(trackIndex, clipIndex);
    QCOMPARE(clip.value(QStringLiteral("kind")).toString(), QStringLiteral("text"));
    QVERIFY(!clip.value(QStringLiteral("textContent")).toString().isEmpty());

    // The playhead must sit inside the clip, or the preview cannot show it.
    const double start = clip.value(QStringLiteral("start")).toDouble();
    const double duration = clip.value(QStringLiteral("duration")).toDouble();
    QVERIFY(state.playheadSeconds() >= start);
    QVERIFY(state.playheadSeconds() < start + duration);
}

// Passing real text keeps the old behaviour: no placeholder, no editor request.
void EditorStateTest::addTextClipWithTextDoesNotRequestEdit()
{
    AssetLibrary library;
    AppController state(&library);
    QSignalSpy spy(&state, &AppController::inlineTextEditRequested);

    state.addTextClip(QStringLiteral("Hello"), 0.0);

    QCOMPARE(spy.count(), 0);
    QCOMPARE(state.clipAt(state.selectedTrack(), state.selectedClip())
                 .value(QStringLiteral("textContent"))
                 .toString(),
             QStringLiteral("Hello"));
}

void EditorStateTest::undoRedoClipAdd()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Undo me"), 0.0);
    QVERIFY(state.undoAvailable());
    state.undo();
    QCOMPARE(state.durationSeconds(), 0.0);
    QVERIFY(state.redoAvailable());
    state.redo();
    QVERIFY(state.durationSeconds() > 0.0);
}

void EditorStateTest::undoTrackMute()
{
    AssetLibrary library;
    AppController state(&library);
    QVERIFY(!state.trackMuted(0));
    state.setTrackMuted(0, true);
    QVERIFY(state.trackMuted(0));
    QVERIFY(state.undoAvailable());
    state.undo();
    QVERIFY(!state.trackMuted(0));
}

void EditorStateTest::undoBookmarkAdd()
{
    AssetLibrary library;
    AppController state(&library);
    QCOMPARE(state.bookmarks().size(), 0);
    state.addBookmark(1.5, QStringLiteral("Test"));
    QCOMPARE(state.bookmarks().size(), 1);
    QVERIFY(state.undoAvailable());
    state.undo();
    QCOMPARE(state.bookmarks().size(), 0);
}

void EditorStateTest::moveTrackReordersAndRemapsSelection()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Top"), 0.0);
    // addTextClip prepends a text track above the default video track.
    QCOMPARE(state.tracks().size(), 2);
    QCOMPARE(state.tracks().at(0).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("text"));
    QCOMPARE(state.tracks().at(1).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("video"));
    QCOMPARE(state.selectedTrack(), 0);

    state.moveTrack(0, 1);
    QCOMPARE(state.tracks().at(0).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("video"));
    QCOMPARE(state.tracks().at(1).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("text"));
    QCOMPARE(state.selectedTrack(), 1);

    QVERIFY(state.undoAvailable());
    state.undo();
    QCOMPARE(state.tracks().at(0).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("text"));
    QCOMPARE(state.tracks().at(1).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("video"));
}

void EditorStateTest::addTrackInsertsEmptyTrackByType()
{
    AssetLibrary library;
    AppController state(&library);
    QCOMPARE(state.tracks().size(), 1);

    state.addTrack(QStringLiteral("audio"));
    QCOMPARE(state.tracks().size(), 2);
    QCOMPARE(state.tracks().at(0).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("audio"));
    QCOMPARE(state.tracks().at(0).toMap().value(QStringLiteral("clips")).toList().size(), 0);

    state.addTrack(QStringLiteral("video"));
    QCOMPARE(state.tracks().size(), 3);
    QCOMPARE(state.tracks().at(0).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("video"));
    QCOMPARE(state.tracks().at(1).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("audio"));

    state.addTrack(QStringLiteral("not-a-type"));
    QCOMPARE(state.tracks().size(), 3);

    QVERIFY(state.undoAvailable());
    state.undo();
    QCOMPARE(state.tracks().size(), 2);
}

// Packaging embeds the derived artifacts and repoints the project at the extraction directory, so
// a matte survives its cache being swept — which is the whole reason the format exists.
void EditorStateTest::packagedProjectCarriesDerivedArtifacts()
{
    // Keeps the extraction directory out of the real app data location.
    QStandardPaths::setTestModeEnabled(true);
    const auto restore = qScopeGuard([] { QStandardPaths::setTestModeEnabled(false); });

    QTemporaryDir sources;
    QVERIFY(sources.isValid());
    const QString mattePath = sources.filePath(QStringLiteral("matte.mp4"));
    {
        QFile matte(mattePath);
        QVERIFY(matte.open(QIODevice::WriteOnly));
        matte.write(QByteArray(1024, '\x7f'));
    }

    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Masked"), 0.0);
    state.setProjectMetadata(QStringLiteral("Packaged"), QStringLiteral("Ada"),
                             QStringLiteral("With a matte"));

    // No QML-facing setter carries a matte path; the segmentation job writes it directly.
    drift::Clip &clip = state.project()->tracks()[0].clips[0];
    clip.mask.shape = drift::MaskShape::Matte;
    clip.mask.mattePath = mattePath;

    QTemporaryDir out;
    QVERIFY(out.isValid());
    const QString bundlePath = out.filePath(QStringLiteral("packaged.drift"));

    QSignalSpy finished(&state, &AppController::packageFinished);
    state.packageProject(QUrl::fromLocalFile(bundlePath));
    QVERIFY(finished.wait(30000));
    QVERIFY2(finished.first().at(0).toBool(), qPrintable(finished.first().at(1).toString()));

    // The matte's own cache is gone, exactly as a sweep would leave it.
    QVERIFY(QFile::remove(mattePath));

    state.newProject();
    state.loadProject(QUrl::fromLocalFile(bundlePath));
    // Opening a bundle extracts its media on a worker thread and only applies the project
    // document once that finishes, so the timeline below is still the empty new project until
    // the load reports in.
    QTRY_COMPARE_WITH_TIMEOUT(state.lastMessage(), QStringLiteral("Project loaded"), 30000);

    QCOMPARE(state.projectMetadata().value(QStringLiteral("title")).toString(),
             QStringLiteral("Packaged"));
    QCOMPARE(state.projectMetadata().value(QStringLiteral("author")).toString(),
             QStringLiteral("Ada"));

    const drift::Clip &loaded = state.project()->tracks().at(0).clips.at(0);
    QVERIFY(loaded.mask.mattePath != mattePath);
    QVERIFY2(QFileInfo::exists(loaded.mask.mattePath), qPrintable(loaded.mask.mattePath));
    QCOMPARE(QFileInfo(loaded.mask.mattePath).size(), 1024);
}

void EditorStateTest::projectPersistenceRoundTrip()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Persist"), 0.0);
    state.setTrackMuted(0, true);
    state.addBookmark(2.0, QStringLiteral("Mark"));
    QCOMPARE(state.tracks().size(), 2); // text + default video

    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());
    tempFile.close();

    state.saveProject(QUrl::fromLocalFile(tempFile.fileName()));
    state.loadProject(QUrl::fromLocalFile(tempFile.fileName()));

    QVERIFY(state.durationSeconds() > 0.0);
    QCOMPARE(state.tracks().size(), 2);
    QCOMPARE(state.tracks().at(0).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("text"));
    QVERIFY(state.trackMuted(0));
    QCOMPARE(state.bookmarks().size(), 1);
}

void EditorStateTest::textStyleBlendModeKeyframesAndEffects()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Hello"), 0.0);

    const int track = state.selectedTrack();
    const int clip = state.selectedClip();
    QVERIFY(track >= 0);
    QVERIFY(clip >= 0);

    // Text style: partial update only touches the given keys.
    state.setTextStyle(track, clip,
                       QVariantMap{{"pixelSize", 120}, {"fontWeight", 300},
                                   {"color", QStringLiteral("#ffff0000")}});
    QVariantMap style = state.selectedClipData().value(QStringLiteral("textStyle")).toMap();
    QCOMPARE(style.value(QStringLiteral("pixelSize")).toInt(), 120);
    QCOMPARE(style.value(QStringLiteral("fontWeight")).toInt(), 300);
    QCOMPARE(style.value(QStringLiteral("color")).toString(), QStringLiteral("#ffff0000"));

    // Nested animation maps patch-merge like any other key.
    state.setTextStyle(track, clip, QVariantMap{{"animIn", QVariantMap{{"kind", QStringLiteral("pop")},
                                                                       {"duration", 0.25}}}});
    style = state.selectedClipData().value(QStringLiteral("textStyle")).toMap();
    const QVariantMap animIn = style.value(QStringLiteral("animIn")).toMap();
    QCOMPARE(animIn.value(QStringLiteral("kind")).toString(), QStringLiteral("pop"));
    QCOMPARE(animIn.value(QStringLiteral("duration")).toDouble(), 0.25);
    QCOMPARE(style.value(QStringLiteral("pixelSize")).toInt(), 120); // untouched

    // Presets overwrite the whole style.
    state.applyTextPreset(track, clip, QStringLiteral("title"));
    style = state.selectedClipData().value(QStringLiteral("textStyle")).toMap();
    QCOMPARE(style.value(QStringLiteral("pixelSize")).toInt(), 96);
    QCOMPARE(style.value(QStringLiteral("fontWeight")).toInt(), 800);

    // Blend mode.
    state.setClipBlendMode(track, clip, QStringLiteral("multiply"));
    QCOMPARE(state.selectedClipData().value(QStringLiteral("blendMode")).toString(), QStringLiteral("multiply"));

    // Keyframes: add, list, remove.
    state.setClipKeyframe(track, clip, QStringLiteral("opacity"), 0.0, 0.5);
    QVariantList keyframes = state.clipKeyframes(track, clip, QStringLiteral("opacity"));
    QCOMPARE(keyframes.size(), 1);
    QCOMPARE(keyframes.first().toMap().value(QStringLiteral("value")).toDouble(), 0.5);

    // WYSIWYG-style preview updates must refresh selectedClipData for the inspector.
    QSignalSpy clipDataSpy(&state, &AppController::selectedClipDataChanged);
    state.beginPreviewDrag(QStringLiteral("Move clip"));
    state.previewSetClipPosition(track, clip, 100.0, 200.0);
    QVERIFY(clipDataSpy.count() >= 1);
    const QVariantMap keys = state.selectedClipData().value(QStringLiteral("keyframes")).toMap();
    const QVariantList xKeys = keys.value(QStringLiteral("x")).toMap().value(QStringLiteral("points")).toList();
    QVERIFY(!xKeys.isEmpty());
    QCOMPARE(xKeys.first().toMap().value(QStringLiteral("value")).toDouble(), 100.0);
    state.commitPreviewDrag();

    state.beginPreviewDrag(QStringLiteral("Resize clip"));
    state.previewSetClipSize(track, clip, 640.0, 360.0);
    state.commitPreviewDrag();
    state.resetClipTransform(track, clip);
    QCOMPARE(state.clipKeyframes(track, clip, QStringLiteral("opacity")).size(), 0);
    // Reset writes a full-canvas layout keyframe at t=0 for each layout track.
    QCOMPARE(state.clipKeyframes(track, clip, QStringLiteral("x")).size(), 1);
    QCOMPARE(state.clipKeyframes(track, clip, QStringLiteral("width")).size(), 1);

    state.removeClipKeyframe(track, clip, QStringLiteral("opacity"), 0.0);
    QCOMPARE(state.clipKeyframes(track, clip, QStringLiteral("opacity")).size(), 0);

    // Effect catalog wiring: add a known effect, tweak its param, remove it.
    const QVariantList catalog = state.effectCatalog();
    QVERIFY(!catalog.isEmpty());

    state.addEffect(track, clip, QStringLiteral("adjust.contrast"));
    QVariantList effects = state.selectedClipData().value(QStringLiteral("effects")).toList();
    QCOMPARE(effects.size(), 1);
    QCOMPARE(state.selectedClipEffects().size(), 1);
    QCOMPARE(effects.first().toMap().value(QStringLiteral("catalogId")).toString(),
             QStringLiteral("adjust.contrast"));

    state.setEffectParam(track, clip, 0, QStringLiteral("contrast"), 2.5);
    effects = state.selectedClipData().value(QStringLiteral("effects")).toList();
    const QVariantList params = effects.first().toMap().value(QStringLiteral("params")).toList();
    QCOMPARE(params.first().toMap().value(QStringLiteral("value")).toDouble(), 2.5);

    // Preview drag coalesces many slider moves into a single undo step.
    state.beginPreviewDrag(QStringLiteral("Edit effect"));
    state.previewSetEffectParam(track, clip, 0, QStringLiteral("contrast"), 1.2);
    state.previewSetEffectParam(track, clip, 0, QStringLiteral("contrast"), 1.8);
    state.commitPreviewDrag();
    effects = state.selectedClipData().value(QStringLiteral("effects")).toList();
    const QVariantList previewParams = effects.first().toMap().value(QStringLiteral("params")).toList();
    QCOMPARE(previewParams.first().toMap().value(QStringLiteral("value")).toDouble(), 1.8);
    QVERIFY(state.undoAvailable());
    state.undo();
    effects = state.selectedClipData().value(QStringLiteral("effects")).toList();
    const QVariantList undoneParams = effects.first().toMap().value(QStringLiteral("params")).toList();
    QCOMPARE(undoneParams.first().toMap().value(QStringLiteral("value")).toDouble(), 2.5);

    state.removeEffect(track, clip, 0);
    QCOMPARE(state.selectedClipData().value(QStringLiteral("effects")).toList().size(), 0);
    QCOMPARE(state.selectedClipEffects().size(), 0);
}

void EditorStateTest::effectParamKeyframes()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Animate me"), 0.0);

    const int track = state.selectedTrack();
    const int clip = state.selectedClip();
    state.addEffect(track, clip, QStringLiteral("adjust.contrast"));

    const QString prop = QStringLiteral("fx.0.contrast");

    // Reading a never-keyed param must not mint a track behind the const accessor.
    QCOMPARE(state.clipKeyframes(track, clip, prop).size(), 0);
    QVariantList params = state.selectedClipData().value(QStringLiteral("effects")).toList()
                              .first().toMap().value(QStringLiteral("params")).toList();
    QCOMPARE(params.first().toMap().value(QStringLiteral("keyframes")).toMap()
                 .value(QStringLiteral("points")).toList().size(), 0);
    QCOMPARE(params.first().toMap().value(QStringLiteral("prop")).toString(), prop);

    // Auto-key off: a slider drag moves the static value without animating the param.
    state.setAutoKeyEnabled(false);
    state.beginPreviewDrag(QStringLiteral("Edit effect"));
    state.previewSetClipKeyframe(track, clip, prop, 0.0, 1.4);
    state.commitPreviewDrag();
    QCOMPARE(state.clipKeyframes(track, clip, prop).size(), 0);
    params = state.selectedClipData().value(QStringLiteral("effects")).toList()
                 .first().toMap().value(QStringLiteral("params")).toList();
    QCOMPARE(params.first().toMap().value(QStringLiteral("value")).toDouble(), 1.4);

    // The diamond forces a key, and the static value tracks it.
    state.setClipKeyframe(track, clip, prop, 0.0, 0.5);
    state.setClipKeyframe(track, clip, prop, 2.0, 2.5);
    QVariantList keys = state.clipKeyframes(track, clip, prop);
    QCOMPARE(keys.size(), 2);
    QCOMPARE(keys.first().toMap().value(QStringLiteral("value")).toDouble(), 0.5);
    QCOMPARE(keys.last().toMap().value(QStringLiteral("seconds")).toDouble(), 2.0);

    // Easing applies to the key at the playhead, not to the whole track, and is reported
    // back on that key rather than on the track it belongs to.
    state.setPlayheadSeconds(0.0);
    state.setKeyframeInterpolation(track, clip, prop, QStringLiteral("ease"));
    keys = state.clipKeyframes(track, clip, prop);
    QCOMPARE(keys.first().toMap().value(QStringLiteral("easing")).toString(),
             QStringLiteral("ease"));
    QVERIFY(!keys.first().toMap().value(QStringLiteral("custom")).toBool());
    // The key at 2.0s was not touched, so it keeps the straight-line default.
    QCOMPARE(keys.last().toMap().value(QStringLiteral("easing")).toString(),
             QStringLiteral("linear"));

    // Dragging a tangent puts the key into a shape no preset describes.
    state.setKeyframeTangents(track, clip, prop, 0.0, 0.0, 0.0, 0.4, 1.7, false);
    keys = state.clipKeyframes(track, clip, prop);
    QVERIFY(keys.first().toMap().value(QStringLiteral("custom")).toBool());
    QVERIFY(std::abs(keys.first().toMap().value(QStringLiteral("outDx")).toDouble() - 0.4) < 1e-6);

    // Hold overrides the tangents when evaluated but does not destroy them, so switching it
    // back off restores the shape the user drew instead of silently flattening it.
    state.setKeyframeHold(track, clip, prop, 0.0, true);
    keys = state.clipKeyframes(track, clip, prop);
    QVERIFY(keys.first().toMap().value(QStringLiteral("hold")).toBool());
    QCOMPARE(keys.first().toMap().value(QStringLiteral("easing")).toString(),
             QStringLiteral("hold"));

    state.setKeyframeHold(track, clip, prop, 0.0, false);
    keys = state.clipKeyframes(track, clip, prop);
    QVERIFY(!keys.first().toMap().value(QStringLiteral("hold")).toBool());
    QVERIFY(std::abs(keys.first().toMap().value(QStringLiteral("outDx")).toDouble() - 0.4) < 1e-6);
    QVERIFY(keys.first().toMap().value(QStringLiteral("custom")).toBool());

    state.removeClipKeyframe(track, clip, prop, 2.0);
    QCOMPARE(state.clipKeyframes(track, clip, prop).size(), 1);

    // An out-of-range effect index resolves to nothing rather than crashing.
    QCOMPARE(state.clipKeyframes(track, clip, QStringLiteral("fx.9.contrast")).size(), 0);
}

void EditorStateTest::effectRemovalRemapsGraphSelection()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Two effects"), 0.0);

    const int track = state.selectedTrack();
    const int clip = state.selectedClip();
    state.addEffect(track, clip, QStringLiteral("adjust.contrast"));
    state.addEffect(track, clip, QStringLiteral("adjust.brightness"));

    state.toggleKeyframeGraphPropertyVisible(QStringLiteral("x"));
    state.toggleKeyframeGraphPropertyVisible(QStringLiteral("fx.0.contrast"));
    state.toggleKeyframeGraphPropertyVisible(QStringLiteral("fx.1.brightness"));
    QCOMPARE(state.keyframeGraphHiddenProperties(),
             (QStringList { QStringLiteral("x"), QStringLiteral("fx.0.contrast"),
                            QStringLiteral("fx.1.brightness") }));

    // Dropping effect 0 must retire its entry and renumber the one above it, or brightness would
    // inherit contrast's folded-away state.
    state.removeEffect(track, clip, 0);
    QCOMPARE(state.keyframeGraphHiddenProperties(),
             (QStringList { QStringLiteral("x"), QStringLiteral("fx.0.brightness") }));
}

void EditorStateTest::previewSetTextRectScalesPixelSize()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Resize me"), 0.0);

    const int track = state.selectedTrack();
    const int clip = state.selectedClip();

    const QVariantMap before = state.selectedClipData().value(QStringLiteral("textStyle")).toMap();
    const int basePixelSize = before.value(QStringLiteral("pixelSize")).toInt();
    QVERIFY(basePixelSize > 0);

    // Dragging a corner handle scales the glyphs with the box, not just the wrap container.
    state.beginPreviewDrag(QStringLiteral("Resize text"));
    state.previewSetTextRect(track, clip, 10.0, 20.0, 800.0, 600.0, basePixelSize * 2);
    state.commitPreviewDrag();

    QVariantMap after = state.selectedClipData().value(QStringLiteral("textStyle")).toMap();
    QCOMPARE(after.value(QStringLiteral("pixelSize")).toInt(), basePixelSize * 2);
    const QVariantMap keys = state.selectedClipData().value(QStringLiteral("keyframes")).toMap();
    const QVariantList widthKeys =
        keys.value(QStringLiteral("width")).toMap().value(QStringLiteral("points")).toList();
    QCOMPARE(widthKeys.first().toMap().value(QStringLiteral("value")).toDouble(), 800.0);

    // The size and the rect land in one undo entry, so a single undo restores both.
    state.undo();
    after = state.selectedClipData().value(QStringLiteral("textStyle")).toMap();
    QCOMPARE(after.value(QStringLiteral("pixelSize")).toInt(), basePixelSize);
}

void EditorStateTest::fontCatalogIsExposedToQml()
{
    AssetLibrary library;
    AppController state(&library);

    const QVariantList catalog = state.fontCatalog();
    if (catalog.isEmpty())
        QSKIP("font bundle not present — run scripts/fetch-fonts.py");

    // Every key the FontPicker delegate and the weight combo read must actually arrive.
    for (const QVariant &entry : catalog) {
        const QVariantMap family = entry.toMap();
        QVERIFY(!family.value(QStringLiteral("family")).toString().isEmpty());
        QVERIFY(!family.value(QStringLiteral("qtFamily")).toString().isEmpty());
        QVERIFY(!family.value(QStringLiteral("categoryLabel")).toString().isEmpty());
        QVERIFY(!family.value(QStringLiteral("weights")).toList().isEmpty());
    }

    const auto findFamily = [&](const QString &name) {
        for (const QVariant &entry : catalog) {
            if (entry.toMap().value(QStringLiteral("family")).toString() == name)
                return entry.toMap();
        }
        return QVariantMap{};
    };

    const QVariantMap anton = findFamily(QStringLiteral("Anton"));
    QCOMPARE(anton.value(QStringLiteral("weights")).toList().size(), 1);
    QCOMPARE(anton.value(QStringLiteral("hasItalic")).toBool(), false);

    const QVariantMap montserrat = findFamily(QStringLiteral("Montserrat"));
    QVERIFY(montserrat.value(QStringLiteral("weights")).toList().size() >= 6);
    QCOMPARE(montserrat.value(QStringLiteral("hasItalic")).toBool(), true);

    QVERIFY(!state.fontCategories().isEmpty());
}

void EditorStateTest::effectBrowserCategoriesAndApply()
{
    AssetLibrary library;
    AppController state(&library);

    // Four built in, plus one per category contributed by the bundled effect packages. Counting
    // exactly would just be a tally of how many packages ship today.
    const QVariantList categories = state.effectCategories();
    QVERIFY(categories.size() >= 5);
    QCOMPARE(categories.first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("glitch"));
    QCOMPARE(categories.first().toMap().value(QStringLiteral("label")).toString(),
             QStringLiteral("Glitch & Distortion"));

    const QVariantList catalog = state.effectCatalog();
    QVERIFY(catalog.size() >= 16);

    QSet<QString> categoryIds;
    for (const QVariant &category : categories)
        categoryIds.insert(category.toMap().value(QStringLiteral("id")).toString());

    for (const QVariant &entry : catalog) {
        const QVariantMap preset = entry.toMap();
        QVERIFY(categoryIds.contains(preset.value(QStringLiteral("category")).toString()));
        QVERIFY(!preset.value(QStringLiteral("categoryLabel")).toString().isEmpty());
    }

    state.addTextClip(QStringLiteral("FX"), 0.0);
    const int track = state.selectedTrack();
    const int clip = state.selectedClip();
    QVERIFY(track >= 0);
    QVERIFY(clip >= 0);

    state.addEffect(track, clip, QStringLiteral("rgb_split"));
    QVariantList effects = state.selectedClipData().value(QStringLiteral("effects")).toList();
    QCOMPARE(effects.size(), 1);
    QCOMPARE(state.selectedClipEffects().size(), 1);
    QCOMPARE(effects.first().toMap().value(QStringLiteral("catalogId")).toString(),
             QStringLiteral("rgb_split"));
    QCOMPARE(effects.first().toMap().value(QStringLiteral("label")).toString(), QStringLiteral("RGB Split"));
    QCOMPARE(state.project()->tracks()[track].clips[clip].effects.size(), 1);

    state.removeEffect(track, clip, 0);
    QCOMPARE(state.selectedClipData().value(QStringLiteral("effects")).toList().size(), 0);
    QCOMPARE(state.selectedClipEffects().size(), 0);
    QCOMPARE(state.project()->tracks()[track].clips[clip].effects.size(), 0);
}

void EditorStateTest::multiSelectClipboardGuidesAndShortcuts()
{
    AssetLibrary library;
    AppController state(&library);

    state.addTextClip(QStringLiteral("A"), 0.0);
    state.addTextClip(QStringLiteral("B"), 2.0);

    const int track = state.selectedTrack();
    QVERIFY(track >= 0);

    // Build a two-clip selection.
    state.selectClip(track, 0);
    state.addToSelection(track, 1);
    QCOMPARE(state.selection().size(), 2);
    QVERIFY(state.selectionContains(track, 0));
    QVERIFY(state.selectionContains(track, 1));

    // Copy/paste at playhead keeps both clips.
    state.setPlayheadSeconds(10.0);
    state.copySelection();
    state.pasteAtPlayhead();
    QCOMPARE(state.tracks().at(track).toMap().value(QStringLiteral("clips")).toList().size(), 4);

    // Nudge and cut do not crash and remain undoable.
    state.nudgeSelection(0.25);
    QVERIFY(state.undoAvailable());
    state.cutSelection();
    QVERIFY(state.tracks().at(track).toMap().value(QStringLiteral("clips")).toList().size() <= 2);

    // Guides state is writable.
    state.setGuidesEnabled(true);
    QCOMPARE(state.guidesEnabled(), true);
    state.setGuideType(QStringLiteral("safe"));
    QCOMPARE(state.guideType(), QStringLiteral("safe"));

    // Shortcut/action layer wiring.
    state.setShortcut(QStringLiteral("nudgeRight"), QStringLiteral("Ctrl+Alt+Right"));
    QCOMPARE(state.shortcutFor(QStringLiteral("nudgeRight")), QStringLiteral("Ctrl+Alt+Right"));

    bool found = false;
    for (const QVariant &entry : state.actions()) {
        const QVariantMap action = entry.toMap();
        if (action.value(QStringLiteral("id")).toString() != QStringLiteral("nudgeRight"))
            continue;
        QCOMPARE(action.value(QStringLiteral("shortcut")).toString(), QStringLiteral("Ctrl+Alt+Right"));
        found = true;
    }
    QVERIFY(found);

    state.triggerAction(QStringLiteral("toggleGuides"));
    QCOMPARE(state.guidesEnabled(), false);
}

static void appendAdjacentShapeClips(drift::Project &project, drift::TimeUs gapUs = 0)
{
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});

    drift::Clip clipA;
    clipA.id = QStringLiteral("clip-a");
    clipA.type = drift::ClipType::Shape;
    clipA.timelineStart = 0;
    clipA.timelineDuration = drift::secondsToUs(2.0);

    drift::Clip clipB;
    clipB.id = QStringLiteral("clip-b");
    clipB.type = drift::ClipType::Shape;
    clipB.timelineStart = clipA.timelineEnd() + gapUs;
    clipB.timelineDuration = drift::secondsToUs(2.0);

    project.tracks()[0].clips.append(clipA);
    project.tracks()[0].clips.append(clipB);
}

static void appendCombinedVideoClip(drift::Project &project)
{
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});

    drift::MediaAsset asset;
    asset.id = QStringLiteral("asset-video");
    asset.path = QStringLiteral("/tmp/video.mp4");
    asset.name = QStringLiteral("Video");
    asset.kind = drift::MediaKind::Video;
    asset.durationUs = drift::secondsToUs(4.0);
    asset.sampleRate = 48000;
    asset.channels = 2;
    project.assets().insert(asset.id, asset);
    project.assetOrder().append(asset.id);

    drift::Clip video;
    video.id = QStringLiteral("clip-video");
    video.assetId = asset.id;
    video.type = drift::ClipType::Video;
    video.name = asset.name;
    video.path = asset.path;
    video.timelineStart = 0;
    video.timelineDuration = drift::secondsToUs(4.0);
    video.srcIn = 0;
    video.srcOut = drift::secondsToUs(4.0);

    project.tracks()[0].clips.append(video);
}

static void appendLinkedVideoAudioPair(drift::Project &project)
{
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});
    project.tracks().append(drift::Track{.type = drift::TrackType::Audio});

    drift::MediaAsset asset;
    asset.id = QStringLiteral("asset-video");
    asset.path = QStringLiteral("/tmp/video.mp4");
    asset.name = QStringLiteral("Video");
    asset.kind = drift::MediaKind::Video;
    asset.durationUs = drift::secondsToUs(4.0);
    asset.sampleRate = 48000;
    asset.channels = 2;
    project.assets().insert(asset.id, asset);
    project.assetOrder().append(asset.id);

    const QString linkId = QStringLiteral("link-1");

    drift::Clip video;
    video.id = QStringLiteral("clip-video");
    video.linkId = linkId;
    video.suppressEmbeddedAudio = true;
    video.assetId = asset.id;
    video.type = drift::ClipType::Video;
    video.name = asset.name;
    video.path = asset.path;
    video.timelineStart = 0;
    video.timelineDuration = drift::secondsToUs(4.0);
    video.srcIn = 0;
    video.srcOut = drift::secondsToUs(4.0);

    drift::Clip audio;
    audio.id = QStringLiteral("clip-audio");
    audio.linkId = linkId;
    audio.assetId = asset.id;
    audio.type = drift::ClipType::Audio;
    audio.name = asset.name;
    audio.path = asset.path;
    audio.timelineStart = 0;
    audio.timelineDuration = drift::secondsToUs(4.0);
    audio.srcIn = 0;
    audio.srcOut = drift::secondsToUs(4.0);

    project.tracks()[0].clips.append(video);
    project.tracks()[1].clips.append(audio);
}

// Separating audio and unlinking are two different operations: a combined clip carries its audio
// inside the video clip and has nothing to unlink, and separating it produces a linked pair that
// still moves together until the link itself is broken.
void EditorStateTest::separateAudioFromCombinedClip()
{
    AssetLibrary library;
    AppController state(&library);
    appendCombinedVideoClip(*state.project());

    QCOMPARE(state.project()->tracks().size(), 1);
    QCOMPARE(state.project()->tracks().at(0).clips.size(), 1);

    state.selectClip(0, 0);
    QVERIFY(state.canSeparateAudioSelection());
    QVERIFY(!state.canUnlinkSelection());
    QVERIFY(state.selectionContains(0, 0));

    state.moveClip(0, 0, 2.0);
    QCOMPARE(state.project()->tracks().at(0).clips.at(0).timelineStart, drift::secondsToUs(2.0));

    state.selectClip(0, 0);
    state.separateAudioFromSelection();
    QCOMPARE(state.project()->tracks().size(), 2);
    QCOMPARE(state.project()->tracks().at(1).clips.size(), 1);
    QCOMPARE(state.project()->tracks().at(1).clips.at(0).timelineStart, drift::secondsToUs(2.0));
    // The video must stop playing the audio it just handed over.
    QCOMPARE(state.project()->tracks().at(0).clips.at(0).suppressEmbeddedAudio, true);

    // The two halves come out linked, so there is now something to unlink.
    state.selectClip(0, 0);
    QVERIFY(state.canUnlinkSelection());
    QVERIFY(!state.canSeparateAudioSelection());
}

void EditorStateTest::linkedAudioUnlinkAndMove()
{
    AssetLibrary library;
    AppController state(&library);
    appendLinkedVideoAudioPair(*state.project());

    QCOMPARE(state.project()->tracks().size(), 2);
    QCOMPARE(state.project()->tracks().at(0).clips.size(), 1);
    QCOMPARE(state.project()->tracks().at(1).clips.size(), 1);

    state.selectClip(0, 0);
    QVERIFY(state.canUnlinkSelection());
    QVERIFY(state.selectionContains(0, 0));

    // Linked: moving the video carries its audio along.
    state.moveClip(0, 0, 2.0);
    QCOMPARE(state.project()->tracks().at(0).clips.at(0).timelineStart, drift::secondsToUs(2.0));
    QCOMPARE(state.project()->tracks().at(1).clips.at(0).timelineStart, drift::secondsToUs(2.0));

    state.selectClip(0, 0);
    state.unlinkSelectedClips();
    QVERIFY(!state.canUnlinkSelection());

    // Unlinked: the audio stays where it was.
    state.moveClip(0, 0, 0.0);
    QCOMPARE(state.project()->tracks().at(0).clips.at(0).timelineStart, drift::secondsToUs(0.0));
    QCOMPARE(state.project()->tracks().at(1).clips.at(0).timelineStart, drift::secondsToUs(2.0));
}

void EditorStateTest::addTransitionBetweenAdjacentClips()
{
    AssetLibrary library;
    AppController state(&library);
    appendAdjacentShapeClips(*state.project(), 500);

    state.selectClip(0, 0);
    state.addTransition(0, 0, QStringLiteral("wipe_left"), 0.75);

    const QVariantMap transition = state.transitionBetweenClips(0, 0);
    QVERIFY(!transition.isEmpty());
    QCOMPARE(transition.value(QStringLiteral("kind")).toString(), QStringLiteral("wipe_left"));
    QCOMPARE(transition.value(QStringLiteral("duration")).toDouble(), 0.75);
}

void EditorStateTest::setTransitionKindAndDurationPersist()
{
    AssetLibrary library;
    AppController state(&library);
    appendAdjacentShapeClips(*state.project());

    state.addTransition(0, 0, QStringLiteral("crossfade"), 0.5);
    const QString transitionId = state.transitionBetweenClips(0, 0).value(QStringLiteral("id")).toString();
    QVERIFY(!transitionId.isEmpty());

    state.setTransitionKind(0, transitionId, QStringLiteral("zoom_in"));
    state.setTransitionDuration(0, transitionId, 1.25);

    const QVariantMap transition = state.transitionBetweenClips(0, 0);
    QCOMPARE(transition.value(QStringLiteral("kind")).toString(), QStringLiteral("zoom_in"));
    QCOMPARE(transition.value(QStringLiteral("duration")).toDouble(), 1.25);
}

void EditorStateTest::replaceTransitionOnDrop()
{
    AssetLibrary library;
    AppController state(&library);
    appendAdjacentShapeClips(*state.project());

    state.addTransition(0, 0, QStringLiteral("crossfade"), 0.5);
    const QString firstId = state.transitionBetweenClips(0, 0).value(QStringLiteral("id")).toString();
    QVERIFY(!firstId.isEmpty());

    state.addTransition(0, 0, QStringLiteral("wipe_right"), 0.5);
    const QVariantMap replaced = state.transitionBetweenClips(0, 0);
    QCOMPARE(replaced.value(QStringLiteral("id")).toString(), firstId);
    QCOMPARE(replaced.value(QStringLiteral("kind")).toString(), QStringLiteral("wipe_right"));
    QCOMPARE(state.project()->tracks().at(0).transitions.size(), 1);
}

void EditorStateTest::overlapAutoAppliesCrossfade()
{
    AssetLibrary library;
    AppController state(&library);
    appendAdjacentShapeClips(*state.project(), -drift::secondsToUs(0.5)); // 0.5s physical overlap

    // Overlap sync runs on finishEdit; nudge via a no-op-ish move to trigger it.
    state.moveClip(0, 1, drift::usToSeconds(state.project()->tracks().at(0).clips.at(1).timelineStart));

    const QVariantMap transition = state.transitionBetweenClips(0, 0);
    QVERIFY(!transition.isEmpty());
    QCOMPARE(transition.value(QStringLiteral("kind")).toString(), QStringLiteral("crossfade"));
    QCOMPARE(transition.value(QStringLiteral("overlapping")).toBool(), true);
    QCOMPARE(transition.value(QStringLiteral("duration")).toDouble(), 0.5);

    state.addTransition(0, 0, QStringLiteral("dip"), 0.5);
    QCOMPARE(state.transitionBetweenClips(0, 0).value(QStringLiteral("kind")).toString(),
             QStringLiteral("dip"));
}

void EditorStateTest::keyframeGraphPropertySelection()
{
    AssetLibrary library;
    AppController state(&library);
    // Nothing is folded away to begin with: the strip shows every animated property of the clip.
    QVERIFY(state.keyframeGraphHiddenProperties().isEmpty());

    // A chip click hides that one curve and leaves the rest alone.
    state.toggleKeyframeGraphPropertyVisible(QStringLiteral("y"));
    QCOMPARE(state.keyframeGraphHiddenProperties(), QStringList { QStringLiteral("y") });
    state.toggleKeyframeGraphPropertyVisible(QStringLiteral("opacity"));
    QCOMPARE(state.keyframeGraphHiddenProperties(),
             (QStringList { QStringLiteral("y"), QStringLiteral("opacity") }));

    // Clicking the same chip again brings the curve back.
    state.toggleKeyframeGraphPropertyVisible(QStringLiteral("y"));
    QCOMPARE(state.keyframeGraphHiddenProperties(), QStringList { QStringLiteral("opacity") });

    // Editing a property un-hides it, and is a no-op for one that was never hidden.
    state.showKeyframeGraphProperty(QStringLiteral("opacity"));
    QVERIFY(state.keyframeGraphHiddenProperties().isEmpty());
    state.showKeyframeGraphProperty(QStringLiteral("width"));
    QVERIFY(state.keyframeGraphHiddenProperties().isEmpty());

    // Effect param keys are verbatim manifest identifiers and are often camelCase, so they must
    // survive normalization intact while bare transform names stay case-insensitive.
    state.toggleKeyframeGraphPropertyVisible(QStringLiteral("fx.0.u_blurRadius"));
    QCOMPARE(state.keyframeGraphHiddenProperties(),
             QStringList { QStringLiteral("fx.0.u_blurRadius") });
    state.toggleKeyframeGraphPropertyVisible(QStringLiteral("X"));
    QCOMPARE(state.keyframeGraphHiddenProperties(),
             (QStringList { QStringLiteral("fx.0.u_blurRadius"), QStringLiteral("x") }));

    // Unknown and malformed keys never reach the strip.
    state.toggleKeyframeGraphPropertyVisible(QStringLiteral("bogus"));
    state.toggleKeyframeGraphPropertyVisible(QStringLiteral("fx."));
    state.toggleKeyframeGraphPropertyVisible(QStringLiteral("fx.a.b"));
    QCOMPARE(state.keyframeGraphHiddenProperties(),
             (QStringList { QStringLiteral("fx.0.u_blurRadius"), QStringLiteral("x") }));
}

// The inspector row's label switches a property's animation off without discarding its keys: the
// property freezes at its first key, and switching it back on restores the animation exactly.
void EditorStateTest::keyframesCanBeDisabledPerProperty()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Animate me"), 0.0);

    const int track = state.selectedTrack();
    const int clip = state.selectedClip();

    state.setClipKeyframe(track, clip, QStringLiteral("opacity"), 0.0, 0.0);
    state.setClipKeyframe(track, clip, QStringLiteral("opacity"), 2.0, 1.0);
    QCOMPARE(state.clipAnimatedProperties(track, clip), QStringList { QStringLiteral("opacity") });
    QVERIFY(state.clipPropertyKeyframesEnabled(track, clip, QStringLiteral("opacity")));
    QCOMPARE(state.propertyValueAt(track, clip, QStringLiteral("opacity"), 2.0, 1.0), 1.0);

    state.toggleClipPropertyKeyframesEnabled(track, clip, QStringLiteral("opacity"));
    QVERIFY(!state.clipPropertyKeyframesEnabled(track, clip, QStringLiteral("opacity")));
    // Frozen at the first key, everywhere.
    QCOMPARE(state.propertyValueAt(track, clip, QStringLiteral("opacity"), 2.0, 1.0), 0.0);
    QCOMPARE(state.propertyValueAt(track, clip, QStringLiteral("opacity"), 1.0, 1.0), 0.0);
    // The keys themselves survive, so the strip still has a curve to draw.
    QCOMPARE(state.clipKeyframes(track, clip, QStringLiteral("opacity")).size(), 2);
    QCOMPARE(state.clipAnimatedProperties(track, clip), QStringList { QStringLiteral("opacity") });

    state.toggleClipPropertyKeyframesEnabled(track, clip, QStringLiteral("opacity"));
    QCOMPARE(state.propertyValueAt(track, clip, QStringLiteral("opacity"), 2.0, 1.0), 1.0);

    // Undo takes the switch back with it.
    state.toggleClipPropertyKeyframesEnabled(track, clip, QStringLiteral("opacity"));
    state.undo();
    QVERIFY(state.clipPropertyKeyframesEnabled(track, clip, QStringLiteral("opacity")));

    // A property with no keys has no animation to switch off.
    state.toggleClipPropertyKeyframesEnabled(track, clip, QStringLiteral("rotation"));
    QVERIFY(state.clipPropertyKeyframesEnabled(track, clip, QStringLiteral("rotation")));
    QVERIFY(!state.clipAnimatedProperties(track, clip).contains(QStringLiteral("rotation")));

    // A single key is the case where switching off changes nothing on screen, so it is also the
    // one most likely to look like a dead click: it still has to toggle both ways.
    state.setClipKeyframe(track, clip, QStringLiteral("rotation"), 1.0, 45.0);
    state.toggleClipPropertyKeyframesEnabled(track, clip, QStringLiteral("rotation"));
    QVERIFY(!state.clipPropertyKeyframesEnabled(track, clip, QStringLiteral("rotation")));
    state.toggleClipPropertyKeyframesEnabled(track, clip, QStringLiteral("rotation"));
    QVERIFY(state.clipPropertyKeyframesEnabled(track, clip, QStringLiteral("rotation")));
}

// End-to-end noise removal through the controller: the whole clip is rendered on a worker thread
// and the result lands on a new audio track directly above the source, as one undoable edit.
void EditorStateTest::denoiseAddsCleanedClipOnTrackAbove()
{
    const QString modelDir = QString::fromUtf8(DRIFT_TEST_DENOISE_MODEL_DIR);
    if (!QDir(modelDir).exists())
        QSKIP("DeepFilterNet3 model not installed");
    qputenv("DRIFT_DENOISE_MODEL_DIR", modelDir.toUtf8());

    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source = dir.filePath(QStringLiteral("noisy.wav"));
    QProcess proc;
    proc.start(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
                        QStringLiteral("-i"),
                        QStringLiteral("anoisesrc=d=3:c=white:r=48000:a=0.1"),
                        QStringLiteral("-ac"), QStringLiteral("2"),
                        QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), source});
    QVERIFY(proc.waitForFinished(60000) && proc.exitCode() == 0);

    AssetLibrary library;
    AppController state(&library);

    drift::Project &project = *state.project();
    drift::Track track{.type = drift::TrackType::Audio};
    drift::Clip clip;
    clip.id = QStringLiteral("src-clip");
    clip.type = drift::ClipType::Audio;
    clip.path = source;
    clip.name = QStringLiteral("Noisy");
    clip.timelineStart = drift::secondsToUs(1.5);
    clip.timelineDuration = drift::secondsToUs(3.0);
    clip.srcIn = 0;
    clip.srcOut = drift::secondsToUs(3.0);
    track.clips.append(clip);
    // A new project comes with default tracks; drop them so the indices below are the ones set up
    // here. A video track sits above the audio one, so "directly above the clip's own track" is
    // distinguishable from "at the very top of the timeline".
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});
    project.tracks().append(track);
    const int sourceTrack = 1;

    QSignalSpy finished(&state, &AppController::denoiseFinished);
    state.applyDenoise(sourceTrack, 0);
    QVERIFY2(finished.wait(180000), "denoise did not finish");
    QCOMPARE(finished.count(), 1);
    QVERIFY2(finished.at(0).at(0).toBool(),
             qPrintable(finished.at(0).at(1).toString()));

    QCOMPARE(project.tracks().size(), 3);
    // Inserted at the source's index, pushing it down — not at the top of the timeline.
    QCOMPARE(project.tracks().at(0).type, drift::TrackType::Video);
    QCOMPARE(project.tracks().at(sourceTrack).type, drift::TrackType::Audio);
    QCOMPARE(project.tracks().at(sourceTrack + 1).clips.at(0).id, QStringLiteral("src-clip"));

    const drift::Clip &out = project.tracks().at(sourceTrack).clips.at(0);
    QVERIFY(out.id != clip.id);
    QVERIFY(out.name.contains(QStringLiteral("denoised")));
    QCOMPARE(out.type, drift::ClipType::Audio);
    QVERIFY(out.assetId.isEmpty());
    QVERIFY2(QFileInfo::exists(out.path), qPrintable(out.path));
    QVERIFY(out.path.endsWith(QStringLiteral(".flac")));
    // Stays put on the timeline, and the source window is rebased into the new media.
    QCOMPARE(out.timelineStart, clip.timelineStart);
    QCOMPARE(out.timelineDuration, clip.timelineDuration);
    QCOMPARE(out.srcIn, drift::TimeUs(0));
    QCOMPARE(out.srcOut, clip.srcOut - clip.srcIn);

    // One undoable edit: the track and the clip go together.
    QVERIFY(state.undoAvailable());
    state.undo();
    QCOMPARE(project.tracks().size(), 2);
    QCOMPARE(project.tracks().at(sourceTrack).clips.at(0).id, QStringLiteral("src-clip"));

    QFile::remove(out.path);
}

void EditorStateTest::shapeStylePartialUpdateAndUndo()
{
    AssetLibrary library;
    AppController state(&library);

    // "circle" is a catalog id, not a ShapeKind name — it must resolve to an ellipse in a square
    // box, since every shape now simply fills its layout rect.
    state.addShapeClip(QStringLiteral("circle"), 0.0);
    const int track = state.selectedTrack();
    const int clip = state.selectedClip();
    QVERIFY(track >= 0);
    QVERIFY(clip >= 0);

    QVariantMap style = state.selectedClipData().value(QStringLiteral("shapeStyle")).toMap();
    QCOMPARE(style.value(QStringLiteral("kind")).toString(), QStringLiteral("ellipse"));
    QCOMPARE(state.selectedClipData().value(QStringLiteral("width")).toDouble(),
             state.selectedClipData().value(QStringLiteral("height")).toDouble());

    // Partial update only touches the given keys.
    state.setShapeStyle(track, clip,
                        QVariantMap{{"fillKind", QStringLiteral("linear")},
                                    {"fill", QStringLiteral("#ff00ff00")},
                                    {"strokeStyle", QStringLiteral("dash")}});
    style = state.selectedClipData().value(QStringLiteral("shapeStyle")).toMap();
    QCOMPARE(style.value(QStringLiteral("fillKind")).toString(), QStringLiteral("linear"));
    QCOMPARE(style.value(QStringLiteral("fill")).toString(), QStringLiteral("#ff00ff00"));
    QCOMPARE(style.value(QStringLiteral("strokeStyle")).toString(), QStringLiteral("dash"));
    QCOMPARE(style.value(QStringLiteral("strokeWidth")).toDouble(), 4.0); // untouched

    // Out-of-range values are clamped rather than stored.
    state.setShapeStyle(track, clip, QVariantMap{{"points", 900}, {"innerRatio", -3.0}});
    style = state.selectedClipData().value(QStringLiteral("shapeStyle")).toMap();
    QCOMPARE(style.value(QStringLiteral("points")).toInt(), 60);
    QCOMPARE(style.value(QStringLiteral("innerRatio")).toDouble(), 0.05);

    // Each committed change is its own undo step.
    state.undo();
    style = state.selectedClipData().value(QStringLiteral("shapeStyle")).toMap();
    QCOMPARE(style.value(QStringLiteral("points")).toInt(), 5);
    QCOMPARE(style.value(QStringLiteral("fillKind")).toString(), QStringLiteral("linear"));

    state.undo();
    style = state.selectedClipData().value(QStringLiteral("shapeStyle")).toMap();
    QCOMPARE(style.value(QStringLiteral("fillKind")).toString(), QStringLiteral("solid"));
}

QTEST_MAIN(EditorStateTest)
#include "tst_editorstate.moc"
