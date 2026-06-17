#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QMap>
#include <Qt>
#include "ui/ClipNodeEditor.h" // for NodeId

class VideoWidget;
class ClipManager;
class QWidget;

/// Handles session save/load, periodic autosave, and crash recovery.
class SessionManager : public QObject {
    Q_OBJECT
public:
    struct RecoveryInfo {
        bool        uncleanShutdown = false;
        QString     autosavePath;
        QStringList backupPaths;
    };

    explicit SessionManager(ClipNodeEditor *editor,
                            VideoWidget    *videoWidget,
                            ClipManager    *clipManager,
                            QWidget        *dialogParent,
                            QObject        *parent = nullptr);

    bool loadFromFile(const QString &path, bool showErrors = true);

    static QString configDirectory();
    static QString autosavePath();
    static QString lockFilePath();
    static QString backupsDirectory();

    static RecoveryInfo checkRecovery();
    static void markRunning();
    static void markCleanExit();

    static constexpr int kDefaultBackupRetention = 10;
    static constexpr int kDefaultAutosaveIntervalMs = 120000; // 2 minutes

    bool writeSessionFile(const QJsonObject &json, const QString &path) const;
    bool saveAutosave(const QJsonObject &json, int backupRetention = kDefaultBackupRetention);
    static QStringList listBackupFiles();
    static void pruneBackups(int keepCount = kDefaultBackupRetention);

    QJsonObject buildJson(int crossfader, int transitionMode,
                          double transitionDuration,
                          NodeId activeNodeA, NodeId activeNodeB,
                          const QMap<NodeId, Qt::Key> &nodeHotkeys,
                          const QString &sessionFilePath = {}) const;

    int    restoredCrossfader()        const { return m_restoredCrossfader; }
    int    restoredTransitionMode()    const { return m_restoredTransitionMode; }
    double restoredTransitionDuration() const { return m_restoredTransitionDuration; }
    NodeId restoredActiveNodeA()       const { return m_restoredActiveNodeA; }
    NodeId restoredActiveNodeB()       const { return m_restoredActiveNodeB; }

    QMap<Qt::Key, NodeId> restoredHotkeys() const { return m_restoredHotkeys; }

signals:
    void sessionLoaded();

private:
    ClipNodeEditor *m_editor;
    VideoWidget    *m_videoWidget;
    ClipManager    *m_clipManager;
    QWidget        *m_dialogParent;

    int    m_restoredCrossfader        = 50;
    int    m_restoredTransitionMode    = 0;
    double m_restoredTransitionDuration = 1.0;
    NodeId m_restoredActiveNodeA       = 0;
    NodeId m_restoredActiveNodeB       = 0;
    QMap<Qt::Key, NodeId> m_restoredHotkeys;
};
