#include "engine/AudioFileWriter.h"
#include "engine/EmojiCatalog.h"
#include "engine/FontCatalog.h"
#include "engine/ReverseProxyCache.h"
#include "models/AddonManager.h"
#include "models/AppController.h"
#include "models/AssetLibrary.h"
#include "models/EditorState.h"
#include "models/FileDialogs.h"
#include "models/UpdateChecker.h"
#include "ClipPreviewImageProvider.h"
#include "DriftImageProvider.h"
#include "SegmentImageProvider.h"
#include "TextStylePreviewImageProvider.h"
#include "preview/PreviewItem.h"

// QApplication (not QGuiApplication) is required so QFileDialog can use the
// native platform file picker, which routes through xdg-desktop-portal.
#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QtQml/qqml.h>

#ifdef Q_OS_ANDROID
#include "core/Project.h"
#include "engine/FrameCompositor.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace {

// On-device render check. With <AppDataLocation>/selftest.json in place, composite one frame and
// write selftest.png beside it instead of starting the UI. This is tools/renderframe moved onto
// the device: it exercises FFmpeg decode, the GLES offscreen context, the shader translation and
// package discovery with no QML, no preview item and no clock in the way, which is the cheapest
// way to find out which of those is broken.
//
// adb push a project there, launch, then adb pull the PNG and compare it against renderframe's
// output for the same project and timestamp on the desktop.
bool runSelfTest()
{
    // Both app data locations, not just the writable one. writableLocation() returns the *internal*
    // /data/user/0/<pkg>/files, which adb cannot push into without a debuggable build; the external
    // /sdcard/Android/data/<pkg>/files is the one that is app-owned, permission-free at every API
    // level, and adb-writable. standardLocations() returns both, so the fixture can land in either.
    QString dir;
    QString projectPath;
    const QStringList candidates =
        QStandardPaths::standardLocations(QStandardPaths::AppDataLocation);
    for (const QString &candidate : candidates) {
        const QString path = QDir(candidate).filePath(QStringLiteral("selftest.json"));
        if (QFile::exists(path)) {
            dir = candidate;
            projectPath = path;
            break;
        }
    }

    // qWarning rather than qInfo: informational categories are filtered out by default on Android,
    // which is how an earlier run of this looked like silence rather than "no fixture present".
    if (projectPath.isEmpty()) {
        qWarning("selftest: no selftest.json in any of: %s",
                 qPrintable(candidates.join(QLatin1String(", "))));
        return false;
    }

    qWarning("selftest: loading %s", qPrintable(projectPath));

    QFile file(projectPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("selftest: cannot open %s", qPrintable(projectPath));
        return true;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        qWarning("selftest: not a JSON object");
        return true;
    }

    QString error;
    drift::Project project = drift::Project::fromJson(doc.object(), &error);
    if (!error.isEmpty()) {
        qWarning("selftest: project load failed: %s", qPrintable(error));
        return true;
    }

    // Overridable so one pushed project can be sampled at several times without editing it.
    const drift::TimeUs timeUs = qEnvironmentVariableIntValue("DRIFT_SELFTEST_TIME_US");

    FrameCompositor compositor;
    compositor.setProject(&project);
    const QImage frame = compositor.compositeAt(timeUs);
    if (frame.isNull()) {
        qWarning("selftest: compositor returned an empty frame at %lld us",
                 static_cast<long long>(timeUs));
        return true;
    }

    const QString outPath = QDir(dir).filePath(QStringLiteral("selftest.png"));
    if (!frame.save(outPath))
        qWarning("selftest: failed to write %s", qPrintable(outPath));
    else
        qWarning("selftest: wrote %s (%dx%d)", qPrintable(outPath), frame.width(), frame.height());

    return true;
}

} // namespace
#endif // Q_OS_ANDROID

int main(int argc, char *argv[])
{
#ifdef Q_OS_ANDROID
    // Android is GLES-only; the desktop 3.3 core profile the engine asks for cannot be created
    // here at all. Both contexts that matter — the Qt Quick scene graph's and the compositor's
    // offscreen one in GlRuntime — must agree on the version before they can share textures, so
    // the default is pinned here rather than left to each of them. GlRuntime::initGlObjects picks
    // the same branch.
    QSurfaceFormat androidFormat;
    androidFormat.setRenderableType(QSurfaceFormat::OpenGLES);
    androidFormat.setVersion(3, 0);
    androidFormat.setDepthBufferSize(0);
    androidFormat.setStencilBufferSize(0);
    QSurfaceFormat::setDefaultFormat(androidFormat);
#endif

    // The compositor renders into an FBO on its own GL context and hands the
    // texture to the scene graph without a readback. That requires both contexts
    // to share objects, and the scene graph to actually be on OpenGL. Both must
    // be set before the QApplication is constructed.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QApplication app(argc, argv);
    QApplication::setApplicationName("CutWire Drift");
    QApplication::setOrganizationName("CutWire Drift");
    // Associates the window with the installed .desktop entry so shells (notably
    // Wayland) can find its icon and app metadata.
    QGuiApplication::setDesktopFileName(QStringLiteral("org.cutwire.Drift"));
    // Title bar / taskbar icon when no desktop entry is available (Windows, and
    // Linux runs from the build tree). The .exe still needs the Windows .rc icon
    // for Explorer and pinned-taskbar identity.
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app/drift.png")));

    // Registering the bundled fonts needs a QGuiApplication, and must happen before the compositor
    // thread starts touching QFontDatabase.
    reloadFontCatalog();
    reloadEmojiCatalog();

    // Noise-removal A/B snippets are scratch. Anything still here is from a previous session that
    // did not get to clean up after itself.
    drift::sweepDenoisePreviews();

#ifdef Q_OS_ANDROID
    qWarning("app data locations: %s",
             qPrintable(QStandardPaths::standardLocations(QStandardPaths::AppDataLocation)
                            .join(QLatin1String(", "))));
    if (runSelfTest())
        return 0;
#endif

    // Reversed proxies are a pure cache: dropping one only costs the clip its smooth playback, so
    // they are pruned to a budget rather than kept forever the way mattes are.
    drift::ReverseProxyCache::instance().load();
    drift::ReverseProxyCache::instance().sweep(drift::ReverseProxyCache::kDefaultMaxBytes);

    qmlRegisterType<PreviewItem>("Drift", 1, 0, "PreviewItem");

    static AssetLibrary assetLibrary;
    static EditorState editorState(&assetLibrary);
    static FileDialogs fileDialogs;
    static AddonManager addonManager;
    static UpdateChecker updateChecker;
    qmlRegisterSingletonInstance("Drift", 1, 0, "AssetLibrary", &assetLibrary);
    qmlRegisterSingletonInstance("Drift", 1, 0, "EditorState", &editorState);
    qmlRegisterSingletonInstance("Drift", 1, 0, "AppController", &editorState);
    qmlRegisterSingletonInstance("Drift", 1, 0, "FileDialogs", &fileDialogs);
    qmlRegisterSingletonInstance("Drift", 1, 0, "Addons", &addonManager);
    qmlRegisterSingletonInstance("Drift", 1, 0, "Updates", &updateChecker);

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("drift"), new DriftImageProvider());
    engine.addImageProvider(QStringLiteral("segment"), new SegmentImageProvider());
    engine.addImageProvider(QStringLiteral("clippreview"), new ClipPreviewImageProvider());
    engine.addImageProvider(QStringLiteral("textstyle"), new TextStylePreviewImageProvider());
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QGuiApplication::exit(-1); }, Qt::QueuedConnection);
    // Main.qml is the desktop layout: a three-pane split sized for >=1280x800, driven by hover and
    // right-click. AndroidMain.qml is the touch entry point and starts as the milestone-1 shell —
    // preview plus transport. The desktop tree is left in place and still compiles, because the
    // touch port reuses its leaf components rather than replacing them.
#ifdef Q_OS_ANDROID
    engine.loadFromModule("Drift", "AndroidMain");
#else
    engine.loadFromModule("Drift", "Main");
#endif

    return app.exec();
}
