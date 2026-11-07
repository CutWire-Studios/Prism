#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <memory>

#include "app/show/Cue.h"

class CueListModel;
class QQuickImageProvider;
class QTimer;
class QUndoStack;
struct NodeThumbStore;

/// The ordered cues of a show and which of them is Live, Staging and being edited. Owns every
/// cue; it knows nothing about the compositor, so taking is the caller's job: it resolves the
/// transition, runs the take and calls swapLiveStaging() when the take commits.
class ShowManager : public QObject {
    Q_OBJECT
public:
    explicit ShowManager(QObject *parent = nullptr);
    ~ShowManager() override;

    // ── Cues ─────────────────────────────────────────────────────────────────
    const QList<Cue *> &cues() const { return m_cues; }
    Cue *cue(const QString &id) const;
    int indexOf(const QString &id) const;
    CueListModel *model() const { return m_model; }
    /// Appends (or inserts at @p index) a cue with Output and Audio Output nodes.
    Cue *addCue(const QString &name = {}, int index = -1);
    Cue *duplicateCue(const QString &id);
    /// Refuses the Live cue and the last cue.
    bool removeCue(const QString &id);
    bool moveCue(int from, int to);
    /// Show-level undo of add/remove/duplicate/reorder/cue-property edits (graph edits keep the
    /// per-cue stacks).
    QUndoStack *undoStack() const { return m_undo; }
    /// Applies @p meta to the cue as one undo step.
    void setCueMeta(const QString &id, const CueMeta &meta);
    /// Drops every cue and starts over with one empty Live cue.
    void reset();

    // ── Roles ────────────────────────────────────────────────────────────────
    Cue *liveCue() const { return cue(m_liveId); }
    Cue *stagingCue() const { return cue(m_stagingId); }
    Cue *editedCue() const { return cue(m_editedId); }
    QString liveCueId() const { return m_liveId; }
    QString stagingCueId() const { return m_stagingId; }
    QString editedCueId() const { return m_editedId; }
    /// Empty @p id clears Staging.
    bool stageCue(const QString &id);
    /// Sets Live directly (no take); Staging is cleared when it is the same cue.
    bool setLiveCue(const QString &id);
    /// A take committed: Live and Staging exchange cues.
    void swapLiveStaging();
    /// The cue after @p id in list order, or null.
    Cue *nextCue(const QString &id) const;

    /// The cue whose graph owns @p nodeId (ids are unique across cues).
    Cue *cueForNode(quint64 nodeId) const;

    // ── Editing ──────────────────────────────────────────────────────────────
    /// Makes @p id the edited cue and enters it.
    bool enterCue(const QString &id);
    void exitCue();
    bool inCue() const { return m_inCue; }

    // ── Transitions ──────────────────────────────────────────────────────────
    TransitionSpec defaultTransition() const { return m_default; }
    void setDefaultTransition(const TransitionSpec &spec);
    /// The staging cue's own incoming transition, else the show default.
    TransitionSpec resolveTransition(const Cue *staging) const;

    // ── End actions ──────────────────────────────────────────────────────────
    /// Restarts the Live cue's auto-advance timer; call after changing its end action.
    void rearmEndTimer();
    /// The Live cue ran out (timer or media end); the caller applies its end action.
    void notifyLiveEnded();
    /// Media of the Live cue ended; ends the cue when it opted in with advanceOnMediaEnd.
    void notifyLiveMediaEnded();

    // ── Shared state ─────────────────────────────────────────────────────────
    /// Output canvas every cue graph lays out against.
    void setCanvasSize(int w, int h);
    QQuickImageProvider *createImageProvider() const;
    /// Live, Staging and the edited cue run their scripts; the rest are paused.
    void updateRuntimeActivity();

    // ── Persistence ──────────────────────────────────────────────────────────
    /// {show:{defaultTransition, state:{liveCueId, stagingCueId, editedCueId}}, cues:[...]}.
    QJsonObject toJson() const;
    /// Replaces the show. False (leaving a fresh single-cue show) when it is missing, empty or
    /// holds a cue graph older than ClipGraph::kGraphVersion.
    bool fromJson(const QJsonObject &root);

signals:
    void cueAdded(Cue *cue);
    void cueAboutToBeRemoved(Cue *cue);
    void cuesReset();
    void liveCueChanged();
    void stagingCueChanged();
    void editedCueChanged();
    void defaultTransitionChanged();
    /// Anything the session should save changed (structure, roles, metadata).
    void changed();
    void thumbnailChanged();
    void cueMetaChanged(Cue *cue);
    void liveEnded();

private:
    Cue *makeCue(const QString &id, int serial, bool withDefaults);
    void fixRoles();
    friend class CueStructureCommand;
    friend class MoveCueCommand;
    /// Re-creates a cue from a toJson() snapshot at @p index.
    Cue *restoreCue(const QJsonObject &snapshot, int index);
    /// Removes without the Live/last-cue guards of removeCue; false when it would leave no Live cue.
    bool eraseCue(const QString &id);
    bool moveCueRaw(int from, int to);

    QList<Cue *> m_cues;
    CueListModel *m_model;
    std::shared_ptr<NodeThumbStore> m_thumbs;
    QTimer *m_endTimer;
    QUndoStack *m_undo;
    QString m_liveId, m_stagingId, m_editedId;
    bool m_inCue = true;
    TransitionSpec m_default;
    int m_nextSerial = 1;
    int m_canvasW = 1280, m_canvasH = 720;
};
