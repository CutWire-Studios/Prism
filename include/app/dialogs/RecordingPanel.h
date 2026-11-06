#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include "app/output/OutputHub.h"
#include "core/graph/ClipGraph.h"

/// Backend of the per-stream recording panel: one row per video / audio track that can be
/// recorded on its own, plus the output folder.
class RecordingPanel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Rows {label, active, elapsed}; index is what toggle() takes.
    Q_PROPERTY(QVariantList videoRows READ videoRows NOTIFY rowsChanged)
    Q_PROPERTY(QVariantList audioRows READ audioRows NOTIFY rowsChanged)
    Q_PROPERTY(QString outputDir READ outputDir WRITE setOutputDir NOTIFY outputDirChanged)

public:
    explicit RecordingPanel(QObject *parent = nullptr);

    QVariantList videoRows() const { return rowsToVariant(m_video); }
    QVariantList audioRows() const { return rowsToVariant(m_audio); }
    QString outputDir() const { return m_outputDir; }
    void setOutputDir(const QString &dir);

    /// Rebuilds both row lists from the graph (probes media files for audio streams).
    Q_INVOKABLE void rebuild();
    /// Re-reads active / elapsed state only.
    Q_INVOKABLE void updateStates();
    Q_INVOKABLE void toggle(bool audio, int index, bool on);

signals:
    void rowsChanged();
    void outputDirChanged();

private:
    struct Row {
        QString label;
        OutputHub::TrackKind kind = OutputHub::TrackKind::Program;
        NodeId nodeId = 0;
        bool active = false;
        qint64 elapsedMs = 0;
    };

    OutputHub *hub() const;
    void refreshStates(QVector<Row> &rows) const;
    static QVariantList rowsToVariant(const QVector<Row> &rows);

    QVector<Row> m_video;
    QVector<Row> m_audio;
    QString m_outputDir;
    bool m_connected = false;
};
