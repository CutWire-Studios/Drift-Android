#include "AndroidUri.h"

#include <QFileInfo>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QMimeDatabase>
#include <QtCore/qcoreapplication_platform.h>
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
