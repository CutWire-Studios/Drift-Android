#include "FileDialogs.h"

#include <QDir>
#include <QFileDialog>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>
#endif

FileDialogs::FileDialogs(QObject *parent) : QObject(parent) {}

namespace {

void applyOpenFilters(QFileDialog &dialog, const QStringList &nameFilters)
{
#ifdef Q_OS_ANDROID
    Q_UNUSED(nameFilters);
    // Android's SAF picker understands MIME types, not "*.mp4" name filters. Broad media MIME
    // types keep the system document UI usable; no READ_MEDIA_* / storage permission is needed
    // because access is granted per URI when the user picks a file.
    dialog.setMimeTypeFilters({
        QStringLiteral("video/*"),
        QStringLiteral("audio/*"),
        QStringLiteral("image/*"),
        // Subtitle import. Providers disagree on what an .srt is — SubRip proper, WebVTT's type,
        // or just text — and a project file has no registered type at all, hence the catch-all.
        QStringLiteral("application/x-subrip"),
        QStringLiteral("text/vtt"),
        QStringLiteral("text/plain"),
        QStringLiteral("*/*"),
    });
#else
    if (!nameFilters.isEmpty())
        dialog.setNameFilters(nameFilters);
#endif
}

} // namespace

QUrl FileDialogs::openFile(const QString &title, const QStringList &nameFilters) const
{
    QFileDialog dialog;
    dialog.setWindowTitle(title);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    applyOpenFilters(dialog, nameFilters);
    if (dialog.exec() != QDialog::Accepted)
        return {};
    const QList<QUrl> urls = dialog.selectedUrls();
    return urls.isEmpty() ? QUrl() : urls.first();
}

QList<QUrl> FileDialogs::openFiles(const QString &title, const QStringList &nameFilters) const
{
    QFileDialog dialog;
    dialog.setWindowTitle(title);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFiles);
    applyOpenFilters(dialog, nameFilters);
    if (dialog.exec() != QDialog::Accepted)
        return {};
    return dialog.selectedUrls();
}

QUrl FileDialogs::saveFile(const QString &title, const QStringList &nameFilters,
                           const QString &suggestedName, const QString &suffix,
                           const QString &initialDirectory) const
{
    QFileDialog dialog;
    dialog.setWindowTitle(title);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    if (!nameFilters.isEmpty())
        dialog.setNameFilters(nameFilters);

    if (!initialDirectory.isEmpty() && QDir(initialDirectory).exists())
        dialog.setDirectory(initialDirectory);


    // The extension is put in the suggested name instead of QFileDialog::setDefaultSuffix: a file
    // exported through the documents portal must not be renamed afterwards, and appending the
    // suffix to what the portal returned writes to a path the portal never registered — the data
    // lands next to the picked file as a hidden entry instead of at the chosen name.
    QString name = suggestedName.trimmed();
    name.replace(QLatin1Char('/'), QLatin1Char('_'));
    name.replace(QLatin1Char('\\'), QLatin1Char('_'));
    if (name.isEmpty())
        name = tr("Untitled");
    if (!suffix.isEmpty())
        name += QLatin1Char('.') + suffix;
    dialog.selectFile(name);

    if (dialog.exec() != QDialog::Accepted)
        return {};
    const QList<QUrl> urls = dialog.selectedUrls();
    return urls.isEmpty() ? QUrl() : urls.first();
}

bool FileDialogs::shareFile(const QUrl &url, const QString &mimeType) const
{
#ifdef Q_OS_ANDROID
    if (url.scheme().compare(QLatin1String("content"), Qt::CaseInsensitive) != 0)
        return false;

    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid())
        return false;

    QJniObject uri = QJniObject::callStaticObjectMethod(
        "android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
        QJniObject::fromString(url.toString(QUrl::FullyEncoded)).object<jstring>());
    if (!uri.isValid())
        return false;

    QString type = mimeType;
    if (type.isEmpty()) {
        QJniObject resolver =
            activity.callObjectMethod("getContentResolver", "()Landroid/content/ContentResolver;");
        if (resolver.isValid()) {
            type = resolver
                       .callObjectMethod("getType", "(Landroid/net/Uri;)Ljava/lang/String;",
                                         uri.object())
                       .toString();
        }
        if (type.isEmpty())
            type = QStringLiteral("*/*");
    }

    QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V",
                      QJniObject::fromString(QStringLiteral("android.intent.action.SEND"))
                          .object<jstring>());
    intent.callObjectMethod("setType", "(Ljava/lang/String;)Landroid/content/Intent;",
                            QJniObject::fromString(type).object<jstring>());
    intent.callObjectMethod(
        "putExtra", "(Ljava/lang/String;Landroid/os/Parcelable;)Landroid/content/Intent;",
        QJniObject::fromString(QStringLiteral("android.intent.extra.STREAM")).object<jstring>(),
        uri.object());
    // Intent.FLAG_GRANT_READ_URI_PERMISSION — without it the receiving app gets a URI it cannot open.
    intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", jint(0x00000001));

    QJniObject chooser = QJniObject::callStaticObjectMethod(
        "android/content/Intent", "createChooser",
        "(Landroid/content/Intent;Ljava/lang/CharSequence;)Landroid/content/Intent;",
        intent.object(), QJniObject::fromString(tr("Share")).object());
    if (!chooser.isValid())
        return false;

    activity.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", chooser.object());
    return !QJniEnvironment().checkAndClearExceptions();
#else
    Q_UNUSED(url);
    Q_UNUSED(mimeType);
    return false;
#endif
}

QUrl FileDialogs::takeLaunchUrl()
{
#ifdef Q_OS_ANDROID
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid())
        return {};

    QJniObject intent = activity.callObjectMethod("getIntent", "()Landroid/content/Intent;");
    if (!intent.isValid())
        return {};

    const QString action =
        intent.callObjectMethod("getAction", "()Ljava/lang/String;").toString();
    if (action != QLatin1String("android.intent.action.VIEW"))
        return {};

    QJniObject data = intent.callObjectMethod("getData", "()Landroid/net/Uri;");
    if (!data.isValid())
        return {};

    intent.callObjectMethod("setData", "(Landroid/net/Uri;)Landroid/content/Intent;",
                            static_cast<jobject>(nullptr));
    QJniEnvironment().checkAndClearExceptions();
    return QUrl(data.toString());
#else
    return {};
#endif
}
