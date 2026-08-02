#include "UpdateChecker.h"

#include "VersionCompare.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>
#include <QUrl>

// Configured in CMakeLists.txt (DRIFT_UPDATE_FEED_URL) and injected as a compile definition, the
// same way the addon service is, so a fork points at its own repository without touching code.
// A translation unit built outside the `drift` target gets no feed rather than a stale one.
#ifndef DRIFT_UPDATE_FEED_URL
#define DRIFT_UPDATE_FEED_URL ""
#endif

namespace {

// Once a day. The feed is GitHub's own API and the check is a single conditional-ish GET, but
// there is no reason to ask more often than releases happen.
constexpr qint64 kCheckIntervalSeconds = 24 * 60 * 60;

// Long enough that the first frame, the project restore and the addon index refresh are all past
// before a socket is opened. Nothing in the app waits on this.
constexpr int kStartupDelayMs = 5000;

constexpr int kTransferTimeoutMs = 15000;

const QString kFeedUrl = QStringLiteral(DRIFT_UPDATE_FEED_URL);
const QString kCurrentVersion = QStringLiteral(DRIFT_VERSION);

QString settingsKey(const char *name)
{
    return QLatin1String("updates/") + QLatin1String(name);
}

} // namespace

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this))
{
    m_skippedVersion = QSettings().value(settingsKey("skippedVersion")).toString();

    if (!supported() || !enabled())
        return;

    const QDateTime last = QSettings().value(settingsKey("lastCheck")).toDateTime();
    if (last.isValid() && last.secsTo(QDateTime::currentDateTimeUtc()) < kCheckIntervalSeconds)
        return;

    QTimer::singleShot(kStartupDelayMs, this, [this] { check(false); });
}

UpdateChecker::~UpdateChecker() = default;

bool UpdateChecker::supported() const
{
    return !kFeedUrl.isEmpty();
}

bool UpdateChecker::enabled() const
{
    return QSettings().value(settingsKey("enabled"), true).toBool();
}

void UpdateChecker::setEnabled(bool enabled)
{
    if (enabled == this->enabled())
        return;
    QSettings().setValue(settingsKey("enabled"), enabled);
    emit enabledChanged();
}

bool UpdateChecker::checking() const
{
    return m_checking;
}

bool UpdateChecker::updateAvailable() const
{
    return !m_latestVersion.isEmpty() && m_latestVersion != m_skippedVersion;
}

QString UpdateChecker::currentVersion() const
{
    return kCurrentVersion;
}

QString UpdateChecker::latestVersion() const
{
    return m_latestVersion;
}

QString UpdateChecker::releaseNotes() const
{
    return m_releaseNotes;
}

QString UpdateChecker::releaseUrl() const
{
    return m_releaseUrl;
}

QString UpdateChecker::status() const
{
    return m_status;
}

void UpdateChecker::setChecking(bool checking)
{
    if (m_checking == checking)
        return;
    m_checking = checking;
    emit checkingChanged();
}

void UpdateChecker::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void UpdateChecker::checkNow()
{
    // Asking explicitly un-skips: the user wants to be told about whatever is out there.
    if (!m_skippedVersion.isEmpty()) {
        m_skippedVersion.clear();
        QSettings().remove(settingsKey("skippedVersion"));
    }
    check(true);
}

void UpdateChecker::skipVersion()
{
    if (m_latestVersion.isEmpty())
        return;
    m_skippedVersion = m_latestVersion;
    QSettings().setValue(settingsKey("skippedVersion"), m_skippedVersion);
    emit resultChanged();
}

void UpdateChecker::openDownloadPage()
{
    if (!m_releaseUrl.isEmpty())
        QDesktopServices::openUrl(QUrl(m_releaseUrl));
}

void UpdateChecker::check(bool manual)
{
    if (m_checking || !supported())
        return;

    setChecking(true);
    setStatus(QString());

    QNetworkRequest request{QUrl(kFeedUrl)};
    // GitHub's API rejects requests that send no User-Agent, and pins response shape to an API
    // version so a future default cannot change the fields parsed below.
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QLatin1String("Drift/") + kCurrentVersion);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kTransferTimeoutMs);

    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manual] {
        reply->deleteLater();
        setChecking(false);

        // A background check that failed says nothing: being offline is not an error the user
        // asked about. Only record the attempt when it actually reached GitHub, so a laptop that
        // launches offline all week still checks the day it has a connection.
        if (reply->error() != QNetworkReply::NoError) {
            if (manual)
                setStatus(tr("Couldn’t check for updates: %1").arg(reply->errorString()));
            return;
        }

        QSettings().setValue(settingsKey("lastCheck"), QDateTime::currentDateTimeUtc());
        applyRelease(reply->readAll(), manual);
    });
}

void UpdateChecker::applyRelease(const QByteArray &json, bool manual)
{
    const QJsonObject release = QJsonDocument::fromJson(json).object();

    // releases/latest already excludes drafts and pre-releases; the checks are here so a feed
    // that ever stops doing so cannot start advertising them.
    const QString tag = release.value(QStringLiteral("tag_name")).toString();
    const bool usable = !tag.isEmpty() && !release.value(QStringLiteral("draft")).toBool()
            && !release.value(QStringLiteral("prerelease")).toBool();
    if (!usable) {
        if (manual)
            setStatus(tr("Couldn’t check for updates: unexpected response."));
        return;
    }

    const QString version = tag.startsWith(QLatin1Char('v')) ? tag.mid(1) : tag;
    if (drift::compareVersions(kCurrentVersion, version) >= 0) {
        // A previously-found update that has since been withdrawn stops being advertised.
        m_latestVersion.clear();
        m_releaseNotes.clear();
        m_releaseUrl.clear();
        emit resultChanged();
        if (manual)
            setStatus(tr("Drift %1 is the latest version.").arg(kCurrentVersion));
        return;
    }

    m_latestVersion = version;
    m_releaseNotes = release.value(QStringLiteral("body")).toString();
    m_releaseUrl = release.value(QStringLiteral("html_url")).toString();
    emit resultChanged();
    if (manual)
        setStatus(tr("Drift %1 is available.").arg(version));
}
