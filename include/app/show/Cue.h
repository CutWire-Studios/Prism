#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <memory>
#include <optional>

class ClipGraph;
class ClipGraphController;
class GraphRuntime;
struct NodeThumbStore;

struct TransitionSpec {
    int mode = 0;
    int durationMs = 1000;

    bool operator==(const TransitionSpec &o) const { return mode == o.mode && durationMs == o.durationMs; }
    QJsonObject toJson() const;
    static TransitionSpec fromJson(const QJsonObject &o);
};

/// The editable, non-graph properties of a cue (what the inspector changes).
struct CueMeta {
    QString name;
    QString color;
    QString notes;
    std::optional<TransitionSpec> incoming;
    int endAction = 0;
    int autoAdvanceMs = 0;
    bool advanceOnMediaEnd = false;

    bool operator==(const CueMeta &o) const {
        return name == o.name && color == o.color && notes == o.notes && incoming == o.incoming
            && endAction == o.endAction && autoAdvanceMs == o.autoAdvanceMs
            && advanceOnMediaEnd == o.advanceOnMediaEnd;
    }
};

/// One item of a show: its own node graph (with its own undo stack and runtime) plus
/// the metadata the cue list needs. Node ids are unique across the cues of a show.
class Cue : public QObject {
    Q_OBJECT
public:
    enum class EndAction { Hold, Loop, AutoAdvance, Black };
    Q_ENUM(EndAction)

    /// @p withDefaults adds the Output and Audio Output nodes every cue carries.
    Cue(const QString &id, int serial, std::shared_ptr<NodeThumbStore> thumbs, bool withDefaults,
        QObject *parent = nullptr);

    const QString &id() const { return m_id; }
    int serial() const { return m_serial; }
    ClipGraph *graph() const { return m_graph; }
    ClipGraphController *controller() const { return m_controller; }
    GraphRuntime *runtime() const { return m_runtime; }

    QString name() const { return m_name; }
    QString color() const { return m_color; }
    QString notes() const { return m_notes; }
    QString thumbnail() const { return m_thumbnail; }
    const std::optional<TransitionSpec> &incoming() const { return m_incoming; }
    EndAction endAction() const { return m_endAction; }
    int autoAdvanceMs() const { return m_autoAdvanceMs; }
    bool advanceOnMediaEnd() const { return m_advanceOnMediaEnd; }

    /// Bumps on every setThumbnail so image URLs can be cache-busted.
    int thumbnailRev() const { return m_thumbRev; }
    CueMeta meta() const;
    void setMeta(const CueMeta &meta);

    /// Empties the graph and re-adds the Output and Audio Output nodes.
    void clearGraph();

    void setName(const QString &name);
    void setColor(const QString &color);
    void setNotes(const QString &notes);
    void setThumbnail(const QString &path);
    void setIncoming(const std::optional<TransitionSpec> &spec);
    void setEndAction(EndAction action);
    void setAutoAdvanceMs(int ms);
    void setAdvanceOnMediaEnd(bool on);

    QJsonObject toJson() const;
    /// False when the stored graph is older than ClipGraph::kGraphVersion.
    bool loadJson(const QJsonObject &o);

    static QString endActionName(EndAction action);
    static EndAction endActionFromName(const QString &name);

signals:
    /// Metadata (not graph contents) changed.
    void metaChanged();
    /// The thumbnail file was (re)written; not a session-dirtying change.
    void thumbnailChanged();

private:
    QString m_id;
    int m_serial;
    ClipGraph *m_graph;
    ClipGraphController *m_controller;
    GraphRuntime *m_runtime;

    QString m_name;
    QString m_color;
    QString m_notes;
    QString m_thumbnail;
    std::optional<TransitionSpec> m_incoming;
    EndAction m_endAction = EndAction::Hold;
    int m_autoAdvanceMs = 0;
    bool m_advanceOnMediaEnd = false;
    int m_thumbRev = 0;
};
