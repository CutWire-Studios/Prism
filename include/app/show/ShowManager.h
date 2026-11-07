#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>
#include <memory>

#include "app/show/Scene.h"

class SceneListModel;
class QQuickImageProvider;
class QTimer;
class QUndoStack;
struct NodeThumbStore;

/// The ordered scenes of a show and which of them is Live, Staging and being edited. Owns every
/// scene; it knows nothing about the compositor, so taking is the caller's job: it resolves the
/// transition, runs the take and calls swapLiveStaging() when the take commits.
class ShowManager : public QObject {
    Q_OBJECT
public:
    explicit ShowManager(QObject *parent = nullptr);
    ~ShowManager() override;

    // ── Scenes ─────────────────────────────────────────────────────────────────
    const QList<Scene *> &scenes() const { return m_scenes; }
    Scene *scene(const QString &id) const;
    int indexOf(const QString &id) const;
    SceneListModel *model() const { return m_model; }
    /// Appends (or inserts at @p index) a scene with Output and Audio Output nodes.
    Scene *addScene(const QString &name = {}, int index = -1);
    Scene *duplicateScene(const QString &id);
    /// Refuses the Live scene and the last scene.
    bool removeScene(const QString &id);
    bool moveScene(int from, int to);
    /// Show-level undo of add/remove/duplicate/reorder/scene-property edits (graph edits keep the
    /// per-scene stacks).
    QUndoStack *undoStack() const { return m_undo; }
    /// Applies @p meta to the scene as one undo step.
    void setSceneMeta(const QString &id, const SceneMeta &meta);
    /// Drops every scene and starts over with one empty Live scene.
    void reset();

    // ── Roles ────────────────────────────────────────────────────────────────
    Scene *liveScene() const { return scene(m_liveId); }
    Scene *stagingScene() const { return scene(m_stagingId); }
    Scene *editedScene() const { return scene(m_editedId); }
    QString liveSceneId() const { return m_liveId; }
    QString stagingSceneId() const { return m_stagingId; }
    QString editedSceneId() const { return m_editedId; }
    /// Empty @p id clears Staging.
    bool stageScene(const QString &id);
    /// Sets Live directly (no take); Staging is cleared when it is the same scene.
    bool setLiveScene(const QString &id);
    /// A take committed: Live and Staging exchange scenes.
    void swapLiveStaging();
    /// The scene after @p id in list order, or null.
    Scene *nextScene(const QString &id) const;

    /// The scene whose graph owns @p nodeId (ids are unique across scenes).
    Scene *sceneForNode(quint64 nodeId) const;

    // ── Scene references ─────────────────────────────────────────────────────
    /// Ids named by the Scene source nodes of scene @p id (unknown ids included).
    QStringList sceneReferences(const QString &id) const;
    /// True when a Scene node in @p host pointing at @p target would make @p host contain itself,
    /// directly (host == target) or through other scenes.
    bool wouldCreateSceneCycle(const QString &host, const QString &target) const;
    /// Live, Staging and (with @p includeEdited) the edited scene, plus every scene reachable from
    /// them through Scene nodes.
    QSet<QString> activeSceneIds(bool includeEdited = true) const;

    // ── Editing ──────────────────────────────────────────────────────────────
    /// Makes @p id the edited scene and enters it.
    bool enterScene(const QString &id);
    void exitScene();
    bool inScene() const { return m_inScene; }

    // ── Transitions ──────────────────────────────────────────────────────────
    TransitionSpec defaultTransition() const { return m_default; }
    void setDefaultTransition(const TransitionSpec &spec);
    /// The staging scene's own incoming transition, else the show default.
    TransitionSpec resolveTransition(const Scene *staging) const;

    // ── End actions ──────────────────────────────────────────────────────────
    /// Restarts the Live scene's auto-advance timer; call after changing its end action.
    void rearmEndTimer();
    /// The Live scene ran out (timer or media end); the caller applies its end action.
    void notifyLiveEnded();
    /// Media of the Live scene ended; ends the scene when it opted in with advanceOnMediaEnd.
    void notifyLiveMediaEnded();

    // ── Shared state ─────────────────────────────────────────────────────────
    /// Output canvas every scene graph lays out against.
    void setCanvasSize(int w, int h);
    QSize canvasSize() const { return {m_canvasW, m_canvasH}; }
    QQuickImageProvider *createImageProvider() const;
    /// Live, Staging and the edited scene run their scripts; the rest are paused.
    void updateRuntimeActivity();

    // ── Persistence ──────────────────────────────────────────────────────────
    /// {show:{defaultTransition, state:{liveSceneId, stagingSceneId, editedSceneId}}, scenes:[...]}.
    QJsonObject toJson() const;
    /// Replaces the show. False (leaving a fresh single-scene show) when it is missing, empty or
    /// holds a scene graph older than ClipGraph::kGraphVersion.
    bool fromJson(const QJsonObject &root);

signals:
    void sceneAdded(Scene *scene);
    void sceneAboutToBeRemoved(Scene *scene);
    void scenesReset();
    void liveSceneChanged();
    void stagingSceneChanged();
    void editedSceneChanged();
    void defaultTransitionChanged();
    /// Anything the session should save changed (structure, roles, metadata).
    void changed();
    void thumbnailChanged();
    void sceneMetaChanged(Scene *scene);
    void liveEnded();

private:
    Scene *makeScene(const QString &id, int serial, bool withDefaults);
    void fixRoles();
    friend class SceneStructureCommand;
    friend class MoveSceneCommand;
    /// Re-creates a scene from a toJson() snapshot at @p index.
    Scene *restoreScene(const QJsonObject &snapshot, int index);
    /// Removes without the Live/last-scene guards of removeScene; false when it would leave no Live scene.
    bool eraseScene(const QString &id);
    bool moveSceneRaw(int from, int to);

    QList<Scene *> m_scenes;
    SceneListModel *m_model;
    std::shared_ptr<NodeThumbStore> m_thumbs;
    QTimer *m_endTimer;
    QUndoStack *m_undo;
    QString m_liveId, m_stagingId, m_editedId;
    bool m_inScene = true;
    TransitionSpec m_default;
    int m_nextSerial = 1;
    int m_canvasW = 1280, m_canvasH = 720;
};
