#include "FileDialogs.h"

#include <QDir>
#include <QFileDialog>

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
