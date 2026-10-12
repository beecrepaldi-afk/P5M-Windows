// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#ifndef CHIAKI_WINDOWSUPDATER_H
#define CHIAKI_WINDOWSUPDATER_H

#include <QCryptographicHash>
#include <QFile>
#include <QObject>
#include <QString>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;
class Settings;

// P5M for Windows: finds a newer release on GitHub and, for the portable
// version, installs it in place. Running files can be renamed on Windows,
// so the old ones become *.old, the new ones take their place and P5M
// restarts; the next start deletes the *.old files. Settings and consoles
// live outside the folder (registry, %APPDATA%), so they are kept. The
// Microsoft Store version updates through the Store and never checks.
class WindowsUpdater : public QObject
{
    Q_OBJECT
    // "" (nothing to do), "available", "downloading", "installing", "failed"
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString version READ version NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    // false: the folder is read-only (e.g. Program Files), install() opens the page.
    Q_PROPERTY(bool canInstall READ canInstall NOTIFY changed)
    // false in the Microsoft Store version, which the Store updates.
    Q_PROPERTY(bool enabled READ enabled CONSTANT)

public:
    WindowsUpdater(Settings *settings, QObject *parent = nullptr);

    QString state() const { return state_; }
    QString version() const { return version_; }
    QString error() const { return error_; }
    int progress() const { return progress_; }
    bool canInstall() const;
    bool enabled() const { return !packaged; }

    // Starts the checks (soon after launch, then twice a day) if allowed.
    void start();

    Q_INVOKABLE void check();
    Q_INVOKABLE void install();
    Q_INVOKABLE void openReleasePage();
    // Hides this version for good; the next newer one is offered again.
    Q_INVOKABLE void skip();

signals:
    void changed();
    void progressChanged();
    // The new files are in place and the new P5M was started: quit now.
    void restartRequested();

private:
    void setState(const QString &state, const QString &error = QString());
    void onReleases(QNetworkReply *reply);
    void onDownloadFinished();
    void extract();
    bool swapFiles(const QString &from, QString *why);
    void cleanupOldFiles();

    Settings *settings;
    QNetworkAccessManager *net = nullptr;
    QNetworkReply *download = nullptr;
    QFile zip_file;
    QCryptographicHash hash { QCryptographicHash::Sha256 };
    QTimer timer;
    bool packaged = false;

    QString state_, version_, error_;
    int progress_ = 0;
    QString page_url, asset_url, asset_digest;
    qint64 asset_size = 0;
};

#endif // CHIAKI_WINDOWSUPDATER_H
