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

    SessionManager(ClipGraph *graph, Compositor *compositor, ClipManager *clipManager,
                   QObject *parent = nullptr);

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

    QJsonObject buildJson(int crossfader, int transitionMode,
                          double transitionDuration,
                          NodeId activeNodeA, NodeId activeNodeB,
                          const QJsonArray &hotkeys,
                          const QString &sessionFilePath = {}) const;

    int    restoredCrossfader()        const { return m_restoredCrossfader; }
    int    restoredTransitionMode()    const { return m_restoredTransitionMode; }
    double restoredTransitionDuration() const { return m_restoredTransitionDuration; }
    NodeId restoredActiveNodeA()       const { return m_restoredActiveNodeA; }
    NodeId restoredActiveNodeB()       const { return m_restoredActiveNodeB; }

    QJsonArray restoredHotkeys() const { return m_restoredHotkeys; }

signals:
    void sessionLoaded();
    void loadFailed(const QString &text);
    void assetsMissing(int count, const QString &notes);

private:
    static void relativizeGraphPaths(QJsonObject &graph, const QDir &sessionDir);

    ClipGraph   *m_graph;
    Compositor  *m_compositor;
    ClipManager *m_clipManager;

    int    m_restoredCrossfader        = 50;
    int    m_restoredTransitionMode    = 0;
    double m_restoredTransitionDuration = 1.0;
    NodeId m_restoredActiveNodeA       = 0;
    NodeId m_restoredActiveNodeB       = 0;
    QJsonArray m_restoredHotkeys;
};
