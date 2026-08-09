#include "AndroidUri.h"

#include <QFileInfo>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QMimeDatabase>
#include <QtCore/qcoreapplication_platform.h>

#include <atomic>
#endif

namespace {

std::unique_ptr<QFile> openTarget(const QString &target, QIODevice::OpenMode mode)
{
    if (target.isEmpty())
        return {};

    auto file = std::make_unique<QFile>(target);
    if (!file->open(mode))
        return {};
    return file;
}

#ifdef Q_OS_ANDROID
QJniObject contentResolver()
{
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid())
        return {};
    return context.callObjectMethod("getContentResolver", "()Landroid/content/ContentResolver;");
}

QJniObject javaUri(const QUrl &url)
{
    return QJniObject::callStaticObjectMethod(
        "android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
        QJniObject::fromString(url.toString(QUrl::FullyEncoded)).object<jstring>());
}

std::atomic<int> g_keepScreenOnHolders{0};

// The flag has to be set on the activity's window from the Android UI thread; setting it from the
// Qt thread silently does nothing.
void setKeepScreenOnFlag(bool on)
{
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([on] {
        QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (!activity.isValid())
            return;
        QJniObject window = activity.callObjectMethod("getWindow", "()Landroid/view/Window;");
        if (!window.isValid())
            return;
        // WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
        constexpr jint kFlagKeepScreenOn = 0x00000080;
        if (on)
            window.callMethod<void>("addFlags", "(I)V", kFlagKeepScreenOn);
        else
            window.callMethod<void>("clearFlags", "(I)V", kFlagKeepScreenOn);
    });
}
#endif

} // namespace

bool AndroidUri::isContentUri(const QUrl &url)
{
    return url.scheme().compare(QLatin1String("content"), Qt::CaseInsensitive) == 0;
}

QString AndroidUri::filePath(const QUrl &url)
{
    if (url.isLocalFile())
        return url.toLocalFile();
    if (isContentUri(url))
        return url.toString(QUrl::FullyEncoded);
    return {};
}

QString AndroidUri::localPath(const QUrl &url)
{
    return url.isLocalFile() ? url.toLocalFile() : QString();
}

std::unique_ptr<QFile> AndroidUri::openForRead(const QUrl &url)
{
    return openTarget(filePath(url), QIODevice::ReadOnly);
}

std::unique_ptr<QFile> AndroidUri::openForWrite(const QUrl &url)
{
    return openTarget(filePath(url), QIODevice::WriteOnly | QIODevice::Truncate);
}

QString AndroidUri::displayName(const QUrl &url)
{
#ifdef Q_OS_ANDROID
    if (isContentUri(url)) {
        QJniObject resolver = contentResolver();
        QJniObject uri = javaUri(url);
        if (!resolver.isValid() || !uri.isValid())
            return url.fileName();

        QJniEnvironment env;
        jobjectArray noStrings = nullptr;
        jstring noString = nullptr;
        QJniObject cursor = resolver.callObjectMethod(
            "query",
            "(Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;"
            "Ljava/lang/String;)Landroid/database/Cursor;",
            uri.object(), noStrings, noString, noStrings, noString);
        env.checkAndClearExceptions();

        QString name;
        if (cursor.isValid()) {
            if (cursor.callMethod<jboolean>("moveToFirst", "()Z")) {
                // OpenableColumns.DISPLAY_NAME
                const jint column = cursor.callMethod<jint>(
                    "getColumnIndex", "(Ljava/lang/String;)I",
                    QJniObject::fromString(QStringLiteral("_display_name")).object<jstring>());
                if (column >= 0) {
                    name = cursor.callObjectMethod("getString", "(I)Ljava/lang/String;", column)
                               .toString();
                }
            }
            cursor.callMethod<void>("close", "()V");
            env.checkAndClearExceptions();
        }

        if (name.isEmpty())
            name = url.fileName();
        if (!QFileInfo(name).suffix().isEmpty())
            return name;

        const QJniObject type = resolver.callObjectMethod(
            "getType", "(Landroid/net/Uri;)Ljava/lang/String;", uri.object());
        env.checkAndClearExceptions();
        const QString suffix = QMimeDatabase().mimeTypeForName(type.toString()).preferredSuffix();
        return suffix.isEmpty() ? name : name + QLatin1Char('.') + suffix;
    }
#endif
    return url.isLocalFile() ? QFileInfo(url.toLocalFile()).fileName() : url.fileName();
}

bool AndroidUri::takePersistableReadPermission(const QUrl &url)
{
#ifdef Q_OS_ANDROID
    if (!isContentUri(url))
        return false;

    QJniObject resolver = contentResolver();
    QJniObject uri = javaUri(url);
    if (!resolver.isValid() || !uri.isValid())
        return false;

    constexpr jint kFlagGrantRead = 0x00000001; // Intent.FLAG_GRANT_READ_URI_PERMISSION
    resolver.callMethod<void>("takePersistableUriPermission", "(Landroid/net/Uri;I)V", uri.object(),
                              kFlagGrantRead);
    // SecurityException when the picker handed out a grant that was never persistable. There is
    // nothing to undo and nothing to report: the URI still reads until the process ends.
    QJniEnvironment env;
    return !env.checkAndClearExceptions();
#else
    Q_UNUSED(url);
    return false;
#endif
}

bool AndroidUri::takePersistableReadWritePermission(const QUrl &url)
{
#ifdef Q_OS_ANDROID
    if (!isContentUri(url))
        return false;

    QJniObject resolver = contentResolver();
    QJniObject uri = javaUri(url);
    if (!resolver.isValid() || !uri.isValid())
        return false;

    constexpr jint kFlagGrantRead = 0x00000001;  // Intent.FLAG_GRANT_READ_URI_PERMISSION
    constexpr jint kFlagGrantWrite = 0x00000002; // Intent.FLAG_GRANT_WRITE_URI_PERMISSION
    resolver.callMethod<void>("takePersistableUriPermission", "(Landroid/net/Uri;I)V", uri.object(),
                              kFlagGrantRead | kFlagGrantWrite);
    QJniEnvironment env;
    if (!env.checkAndClearExceptions())
        return true;

    // A call that throws takes nothing at all, not the half it could honour, so asking for both on
    // an ACTION_OPEN_DOCUMENT result (read-only by nature) would lose the read grant too. Re-take
    // read alone; the document stays openable, it just cannot be saved in place.
    return takePersistableReadPermission(url);
#else
    Q_UNUSED(url);
    return false;
#endif
}

bool AndroidUri::deleteDocument(const QUrl &url)
{
#ifdef Q_OS_ANDROID
    if (!isContentUri(url))
        return false;

    QJniObject resolver = contentResolver();
    QJniObject uri = javaUri(url);
    if (!resolver.isValid() || !uri.isValid())
        return false;

    QJniEnvironment env;
    const jboolean deleted = QJniObject::callStaticMethod<jboolean>(
        "android/provider/DocumentsContract", "deleteDocument",
        "(Landroid/content/ContentResolver;Landroid/net/Uri;)Z", resolver.object(), uri.object());
    // FileNotFoundException for a document that is already gone, SecurityException for a provider
    // that never granted delete. Either way the truncate below is what is left to try.
    if (deleted && !env.checkAndClearExceptions())
        return true;
    env.checkAndClearExceptions();

    // Providers are free to refuse deletion (no FLAG_SUPPORTS_DELETE). Opening for write truncates,
    // so what stays behind is an empty file rather than a half-written one that reads as a playable
    // video and plays as garbage.
    return openForWrite(url) != nullptr;
#else
    Q_UNUSED(url);
    return false;
#endif
}

void drift::android::acquireKeepScreenOn()
{
#ifdef Q_OS_ANDROID
    if (g_keepScreenOnHolders.fetch_add(1, std::memory_order_acq_rel) == 0)
        setKeepScreenOnFlag(true);
#endif
}

void drift::android::releaseKeepScreenOn()
{
#ifdef Q_OS_ANDROID
    if (g_keepScreenOnHolders.fetch_sub(1, std::memory_order_acq_rel) == 1)
        setKeepScreenOnFlag(false);
#endif
}
