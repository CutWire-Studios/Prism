#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QMap>
#include <Qt>
#include "core/graph/ClipGraph.h"

class Compositor;
class BpmClock;
class ShowManager;
class ClipManager;

/// Handles session save/load, periodic autosave, and crash recovery.
class SessionManager : public QObject {
    Q_OBJECT
public:
    struct RecoveryInfo {
        bool        uncleanShutdown = false;
        QString     autosavePath;
        QStringList backupPaths;
    };

    SessionManager(ShowManager *show, Compositor *compositor, ClipManager *clipManager,
                   QObject *parent = nullptr);

    /// Optional: the session then stores and restores the global tempo ("bpm"; missing = 120).
    void setBpmClock(BpmClock *clock) { m_bpmClock = clock; }

    bool loadFromFile(const QString &path);

    static QString configDirectory();
    static QString autosavePath();
    static QString lockFilePath();
    static QString backupsDirectory();

    static RecoveryInfo checkRecovery();
    static void markRunning();
    static void markCleanExit();

    static constexpr const char *kSessionExtension  = ".psm";
    static constexpr const char *kAutosaveFileName  = "session.psm";

    static constexpr int kDefaultBackupRetention = 10;
    static constexpr int kDefaultAutosaveIntervalMs = 120000; // 2 minutes

    bool writeSessionFile(const QJsonObject &json, const QString &path) const;
    bool saveAutosave(const QJsonObject &json, int backupRetention = kDefaultBackupRetention);
    static QStringList listBackupFiles();
    static void pruneBackups(int keepCount = kDefaultBackupRetention);

    static constexpr int kFormatVersion = 4;

    QJsonObject buildJson(int transitionProgress, const QJsonArray &hotkeys,
                          const QString &sessionFilePath = {}) const;

    int    restoredTransitionProgress()  const { return m_restoredTransitionProgress; }

    QJsonArray restoredHotkeys() const { return m_restoredHotkeys; }

signals:
    void sessionLoaded();
    void loadFailed(const QString &text);
    void assetsMissing(int count, const QString &notes);

private:
    static void relativizeGraphPaths(QJsonObject &graph, const QDir &sessionDir);

    BpmClock *m_bpmClock = nullptr;
    ShowManager *m_show;
    Compositor  *m_compositor;
    ClipManager *m_clipManager;

    int    m_restoredTransitionProgress = 0;
    QJsonArray m_restoredHotkeys;
};
