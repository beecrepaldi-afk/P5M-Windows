// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "windowsUpdater.h"
#include "settings.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QUrl>

#include <cstring>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <appmodel.h>

Q_DECLARE_LOGGING_CATEGORY(chiakiGui);

#define P5M_RELEASES_API "https://api.github.com/repos/beecrepaldi-afk/P5M-Windows/releases?per_page=20"
#define P5M_RELEASES_PAGE "https://github.com/beecrepaldi-afk/P5M-Windows/releases"
#define P5M_TAG_PREFIX "win-v"
#define P5M_ASSET_SUFFIX "-win_x64-portable.zip"
#define P5M_STAGING ".p5m-update"
#define CHECK_FIRST_MS (5 * 1000)
#define CHECK_EVERY_MS (12 * 60 * 60 * 1000)

namespace {

// "0.1.0-alpha.2" style versions: the numbers first, then a release without
// a suffix is newer than any alpha/beta/rc of it; suffix parts compare as
// numbers when both are numbers ("alpha.10" > "alpha.9").
int compareVersions(const QString &a, const QString &b)
{
    const QString a_main = a.section(QLatin1Char('-'), 0, 0), b_main = b.section(QLatin1Char('-'), 0, 0);
    const QString a_pre = a.section(QLatin1Char('-'), 1), b_pre = b.section(QLatin1Char('-'), 1);
    const QStringList an = a_main.split(QLatin1Char('.')), bn = b_main.split(QLatin1Char('.'));
    for (int i = 0; i < qMax(an.size(), bn.size()); i++) {
        const int x = i < an.size() ? an[i].toInt() : 0, y = i < bn.size() ? bn[i].toInt() : 0;
        if (x != y)
            return x < y ? -1 : 1;
    }
    if (a_pre.isEmpty() || b_pre.isEmpty())
        return a_pre.isEmpty() == b_pre.isEmpty() ? 0 : (a_pre.isEmpty() ? 1 : -1);
    const QStringList ap = a_pre.split(QLatin1Char('.')), bp = b_pre.split(QLatin1Char('.'));
    for (int i = 0; i < qMax(ap.size(), bp.size()); i++) {
        if (i >= ap.size()) return -1;
        if (i >= bp.size()) return 1;
        bool an_ok = false, bn_ok = false;
        const int x = ap[i].toInt(&an_ok), y = bp[i].toInt(&bn_ok);
        const int c = (an_ok && bn_ok) ? (x < y ? -1 : (x > y ? 1 : 0)) : ap[i].compare(bp[i], Qt::CaseInsensitive);
        if (c != 0)
            return c < 0 ? -1 : 1;
    }
    return 0;
}

QString appDir()
{
    return QCoreApplication::applicationDirPath();
}

} // namespace

WindowsUpdater::WindowsUpdater(Settings *settings, QObject *parent)
    : QObject(parent), settings(settings)
{
    UINT32 len = 0;
    packaged = GetCurrentPackageFullName(&len, nullptr) != APPMODEL_ERROR_NO_PACKAGE;
    timer.setSingleShot(false);
    connect(&timer, &QTimer::timeout, this, &WindowsUpdater::check);
}

bool WindowsUpdater::canInstall() const
{
    if (packaged)
        return false;
    QFile probe(QDir(appDir()).filePath(QStringLiteral(".p5m-write-test")));
    if (!probe.open(QIODevice::WriteOnly))
        return false;
    probe.close();
    probe.remove();
    return true;
}

void WindowsUpdater::start()
{
    if (packaged)
        return;
    // The previous update's leftovers: the old P5M had them open until it quit.
    QTimer::singleShot(10 * 1000, this, &WindowsUpdater::cleanupOldFiles);
    QTimer::singleShot(CHECK_FIRST_MS, this, &WindowsUpdater::check);
    timer.start(CHECK_EVERY_MS);
}

void WindowsUpdater::setState(const QString &state, const QString &error)
{
    state_ = state;
    error_ = error;
    emit changed();
}

void WindowsUpdater::check()
{
    if (packaged || !settings->GetCheckForUpdates())
        return;
    if (state_ == QLatin1String("downloading") || state_ == QLatin1String("installing"))
        return;
    if (!net)
        net = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl(QStringLiteral(P5M_RELEASES_API)));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("P5M-Windows/%1").arg(QCoreApplication::applicationVersion()));
    request.setTransferTimeout(30 * 1000);
    QNetworkReply *reply = net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onReleases(reply); });
}

void WindowsUpdater::onReleases(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        qCInfo(chiakiGui) << "Update check failed:" << reply->errorString();
        return;
    }
    const QString current = QCoreApplication::applicationVersion();
    QString best;
    QJsonObject best_asset;
    QString best_page;
    const QJsonArray releases = QJsonDocument::fromJson(reply->readAll()).array();
    for (const QJsonValue &value : releases) {
        const QJsonObject release = value.toObject();
        const QString tag = release.value(QStringLiteral("tag_name")).toString();
        if (release.value(QStringLiteral("draft")).toBool() || !tag.startsWith(QStringLiteral(P5M_TAG_PREFIX)))
            continue;
        const QString version = tag.mid(int(strlen(P5M_TAG_PREFIX)));
        if (compareVersions(version, current) <= 0 || (!best.isEmpty() && compareVersions(version, best) <= 0))
            continue;
        for (const QJsonValue &a : release.value(QStringLiteral("assets")).toArray()) {
            const QJsonObject asset = a.toObject();
            if (asset.value(QStringLiteral("name")).toString().endsWith(QStringLiteral(P5M_ASSET_SUFFIX))) {
                best = version;
                best_asset = asset;
                best_page = release.value(QStringLiteral("html_url")).toString();
                break;
            }
        }
    }
    if (best.isEmpty()) {
        qCInfo(chiakiGui) << "Update check: P5M" << current << "is the newest";
        return;
    }
    if (best == version_ && !state_.isEmpty())
        return;
    if (best == settings->GetSkippedUpdate()) {
        qCInfo(chiakiGui) << "Update check: P5M" << best << "is available but was skipped";
        return;
    }
    version_ = best;
    page_url = best_page;
    asset_url = best_asset.value(QStringLiteral("browser_download_url")).toString();
    asset_size = qint64(best_asset.value(QStringLiteral("size")).toDouble());
    asset_digest = best_asset.value(QStringLiteral("digest")).toString();
    qCInfo(chiakiGui) << "Update check: P5M" << best << "is available (this is" << current << ")";
    setState(QStringLiteral("available"));
}

void WindowsUpdater::skip()
{
    if (state_ != QLatin1String("available") && state_ != QLatin1String("failed"))
        return;
    settings->SetSkippedUpdate(version_);
    qCInfo(chiakiGui) << "Update: P5M" << version_ << "skipped";
    setState(QString());
}

void WindowsUpdater::openReleasePage()
{
    QDesktopServices::openUrl(QUrl(page_url.isEmpty() ? QStringLiteral(P5M_RELEASES_PAGE) : page_url));
}

void WindowsUpdater::install()
{
    if (state_ != QLatin1String("available") && state_ != QLatin1String("failed"))
        return;
    if (!canInstall() || asset_url.isEmpty()) {
        openReleasePage();
        return;
    }
    QDir staging(QDir(appDir()).filePath(QStringLiteral(P5M_STAGING)));
    staging.removeRecursively();
    if (!QDir().mkpath(staging.absolutePath())) {
        setState(QStringLiteral("failed"), tr("Could not write next to P5M."));
        return;
    }
    zip_file.setFileName(staging.filePath(QStringLiteral("update.zip")));
    if (!zip_file.open(QIODevice::WriteOnly)) {
        setState(QStringLiteral("failed"), tr("Could not write next to P5M."));
        return;
    }
    hash.reset();
    progress_ = 0;
    emit progressChanged();
    setState(QStringLiteral("downloading"));
    qCInfo(chiakiGui) << "Update: downloading P5M" << version_;

    QNetworkRequest request{QUrl(asset_url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("P5M-Windows/%1").arg(QCoreApplication::applicationVersion()));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    download = net->get(request);
    connect(download, &QNetworkReply::readyRead, this, [this]() {
        const QByteArray data = download->readAll();
        hash.addData(data);
        zip_file.write(data);
    });
    connect(download, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (total <= 0)
            total = asset_size;
        const int p = total > 0 ? int(received * 100 / total) : 0;
        if (p != progress_) {
            progress_ = p;
            emit progressChanged();
        }
    });
    connect(download, &QNetworkReply::finished, this, &WindowsUpdater::onDownloadFinished);
}

void WindowsUpdater::onDownloadFinished()
{
    QNetworkReply *reply = download;
    download = nullptr;
    reply->deleteLater();
    const QByteArray rest = reply->readAll();
    hash.addData(rest);
    zip_file.write(rest);
    zip_file.close();
    if (reply->error() != QNetworkReply::NoError) {
        qCWarning(chiakiGui) << "Update: download failed:" << reply->errorString();
        setState(QStringLiteral("failed"), tr("The download failed. Check the connection and try again."));
        return;
    }
    if (asset_size > 0 && zip_file.size() != asset_size) {
        qCWarning(chiakiGui) << "Update: size" << zip_file.size() << "instead of" << asset_size;
        setState(QStringLiteral("failed"), tr("The download was incomplete. Try again."));
        return;
    }
    // GitHub publishes the SHA-256 of each release file.
    if (asset_digest.startsWith(QStringLiteral("sha256:"))) {
        const QString got = QString::fromLatin1(hash.result().toHex());
        if (got.compare(asset_digest.mid(7), Qt::CaseInsensitive) != 0) {
            qCWarning(chiakiGui) << "Update: checksum mismatch";
            setState(QStringLiteral("failed"), tr("The download is damaged. Try again."));
            return;
        }
    }
    extract();
}

void WindowsUpdater::extract()
{
    setState(QStringLiteral("installing"));
    const QDir staging(QDir(appDir()).filePath(QStringLiteral(P5M_STAGING)));
    const QString out = staging.filePath(QStringLiteral("files"));
    QDir().mkpath(out);
    // Windows 10 1803+ ships bsdtar as tar.exe, which reads ZIP files.
    auto *tar = new QProcess(this);
    connect(tar, &QProcess::finished, this, [this, tar, out](int code, QProcess::ExitStatus status) {
        tar->deleteLater();
        QFile::remove(zip_file.fileName());
        if (status != QProcess::NormalExit || code != 0) {
            qCWarning(chiakiGui) << "Update: unpacking failed," << code;
            setState(QStringLiteral("failed"), tr("Could not unpack the update."));
            return;
        }
        // The ZIP holds one folder (P5M-Win) with P5M.exe inside.
        QString root;
        if (QFileInfo::exists(QDir(out).filePath(QStringLiteral("P5M.exe"))))
            root = out;
        else
            for (const QString &dir : QDir(out).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
                if (QFileInfo::exists(QDir(out).filePath(dir + QStringLiteral("/P5M.exe"))))
                    root = QDir(out).filePath(dir);
        if (root.isEmpty()) {
            setState(QStringLiteral("failed"), tr("The update has no P5M.exe."));
            return;
        }
        QString why;
        if (!swapFiles(root, &why)) {
            qCWarning(chiakiGui) << "Update: could not replace the files:" << why;
            setState(QStringLiteral("failed"), tr("Could not replace the files. Close other copies of P5M and try again."));
            return;
        }
        qCInfo(chiakiGui) << "Update: P5M" << version_ << "installed, restarting";
        QProcess::startDetached(QDir(appDir()).filePath(QStringLiteral("P5M.exe")), {}, appDir());
        emit restartRequested();
    });
    tar->start(QStringLiteral("tar.exe"), { QStringLiteral("-x"), QStringLiteral("-f"), QDir::toNativeSeparators(zip_file.fileName()),
        QStringLiteral("-C"), QDir::toNativeSeparators(out) });
}

bool WindowsUpdater::swapFiles(const QString &from, QString *why)
{
    const QDir src(from), dst(appDir());
    struct Done { QString target, old; };
    QList<Done> done;
    auto rollback = [&done]() {
        for (auto it = done.crbegin(); it != done.crend(); ++it) {
            QFile::remove(it->target);
            if (!it->old.isEmpty())
                QFile::rename(it->old, it->target);
        }
    };
    QDirIterator it(from, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        const QString rel = src.relativeFilePath(file);
        const QString target = dst.filePath(rel);
        QDir().mkpath(QFileInfo(target).absolutePath());
        QString old;
        if (QFileInfo::exists(target)) {
            // A file in use can't be deleted but can be renamed.
            old = target + QStringLiteral(".old");
            for (int n = 1; QFileInfo::exists(old) && !QFile::remove(old); n++)
                old = target + QStringLiteral(".%1.old").arg(n);
            if (!QFile::rename(target, old)) {
                *why = QStringLiteral("rename ") + rel;
                rollback();
                return false;
            }
        }
        done.append({ target, old });
        if (!QFile::rename(file, target) && !QFile::copy(file, target)) {
            *why = QStringLiteral("place ") + rel;
            rollback();
            return false;
        }
    }
    return !done.isEmpty();
}

void WindowsUpdater::cleanupOldFiles()
{
    QDir(QDir(appDir()).filePath(QStringLiteral(P5M_STAGING))).removeRecursively();
    QDirIterator it(appDir(), { QStringLiteral("*.old") }, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext())
        QFile::remove(it.next());
}
