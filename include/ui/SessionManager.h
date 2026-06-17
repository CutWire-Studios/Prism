#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QMap>
#include <Qt>
#include "ui/ClipNodeEditor.h" // for NodeId

class VideoWidget;
class ClipManager;
class QWidget;

/// Handles session save/load and autosave for the SwitchX application.
/// Extracted from MainWindow to give it a single, clear responsibility.
class SessionManager : public QObject {
    Q_OBJECT
public:
    explicit SessionManager(ClipNodeEditor *editor,
                            VideoWidget    *videoWidget,
                            ClipManager    *clipManager,
                            QWidget        *dialogParent,
                            QObject        *parent = nullptr);

    bool saveToFile(const QString &path) const;
    bool loadFromFile(const QString &path, bool showErrors = true);

    static QString autosavePath();

    // Getters for state restored after a load (consumed by MainWindow).
    int    restoredCrossfader()        const { return m_restoredCrossfader; }
    int    restoredTransitionMode()    const { return m_restoredTransitionMode; }
    double restoredTransitionDuration() const { return m_restoredTransitionDuration; }
    NodeId restoredActiveNodeA()       const { return m_restoredActiveNodeA; }
    NodeId restoredActiveNodeB()       const { return m_restoredActiveNodeB; }

    // Provides the hotkey map (key → nodeId) restored from file.
    // HotkeyManager::restoreHotkeys() consumes this.
    QMap<Qt::Key, NodeId> restoredHotkeys() const { return m_restoredHotkeys; }

signals:
    void sessionLoaded();

public:
    // Called by MainWindow to build the JSON (needs access to UI state).
    QJsonObject buildJson(int crossfader, int transitionMode,
                          double transitionDuration,
                          NodeId activeNodeA, NodeId activeNodeB,
                          const QMap<NodeId, Qt::Key> &nodeHotkeys,
                          const QString &sessionFilePath = {}) const;

private:
    ClipNodeEditor *m_editor;
    VideoWidget    *m_videoWidget;
    ClipManager    *m_clipManager;
    QWidget        *m_dialogParent;

    // State restored on the last loadFromFile() call.
    int    m_restoredCrossfader        = 50;
    int    m_restoredTransitionMode    = 0;
    double m_restoredTransitionDuration = 1.0;
    NodeId m_restoredActiveNodeA       = 0;
    NodeId m_restoredActiveNodeB       = 0;
    QMap<Qt::Key, NodeId> m_restoredHotkeys;
};
