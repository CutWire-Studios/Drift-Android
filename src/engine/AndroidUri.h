#pragma once

#include <QFile>
#include <QString>
#include <QUrl>

#include <memory>

// Everything the user picks on Android arrives as a content:// URI from the Storage Access
// Framework, and QUrl::toLocalFile() returns an empty string for those — the pattern
// `url.toLocalFile()`, bail if empty` fails unconditionally on device. Route file-dialog URLs
// through here instead. On desktop every function collapses to plain local-file behaviour.
namespace AndroidUri {

bool isContentUri(const QUrl &url);

// A string QFile accepts: the local path for file:// URLs, and the fully encoded URI for
// content:// ones, which Qt's AndroidContentFileEngine knows how to open. Empty for anything
// else.
QString filePath(const QUrl &url);

// The real filesystem path, or empty when there is none — which is every content:// URI. This
// is the one callers need before handing a path to FFmpeg or any other non-Qt library.
QString localPath(const QUrl &url);

// Open and ready, or null when the URL cannot be opened. Writing truncates, and the document
// behind a content:// URI has to exist already: ACTION_CREATE_DOCUMENT has created it by the
// time its URI comes back from the dialog.
std::unique_ptr<QFile> openForRead(const QUrl &url);
std::unique_ptr<QFile> openForWrite(const QUrl &url);

// The provider's DISPLAY_NAME — the name the user knows the file by, as opposed to the opaque
// document id that is all the URI carries. An extension is appended from the document's MIME
// type when the provider's name has none, because kind detection downstream is suffix-driven.
// Falls back to the URL's own file name.
QString displayName(const QUrl &url);

// Upgrades the one-shot grant that came with the picker result into one that survives a
// restart, so a stored content:// URI is still readable tomorrow. False when the provider
// offered no persistable grant; the URI then simply expires with the process.
bool takePersistableReadPermission(const QUrl &url);

// The same, for a document the app has to write again later — a project file it will save in
// place. Persisting read alone is what makes Save fail with "could not write to the location you
// picked" on the next launch, since the write grant died with the process. True only when the
// write grant was persisted; false leaves the read grant taken, so the document still opens.
bool takePersistableReadWritePermission(const QUrl &url);

// Disposes of the document behind a content:// URI, for an export or save that never produced
// anything: ACTION_CREATE_DOCUMENT has already created the file by the time the picker returns,
// so abandoning the job silently leaves a 0-byte video in the user's folder. False when neither
// the delete nor the truncate fallback got anywhere.
bool deleteDocument(const QUrl &url);

} // namespace AndroidUri

// Android dims and locks on its own idle timer, and a preview or a render that runs for minutes
// without touch input is exactly the case it gets wrong. FLAG_KEEP_SCREEN_ON says otherwise, but it
// is one bit on the single activity window with two independent owners — playback and a running
// job — and whoever released first used to clear it out from under the other, so pausing the
// preview mid-export let the display sleep. Refcounted here: the flag goes on with the first
// acquire and comes off only when the last holder lets go. No-ops on desktop.
namespace drift::android {

void acquireKeepScreenOn();
void releaseKeepScreenOn();

} // namespace drift::android
