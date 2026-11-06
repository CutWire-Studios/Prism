#include "app/dialogs/RecordingPanel.h"

#include "app/AppController.h"
#include "core/media/MediaProbe.h"
#include "core/sources/SourceDescriptor.h"
#include "ui/recording/RecordingOptions.h"

#include <QVariantMap>

RecordingPanel::RecordingPanel(QObject *parent)
    : QObject(parent)
    , m_outputDir(RecordingOptions::loadSaved().effectiveOutputDir())
{
}

OutputHub *RecordingPanel::hub() const
{
    auto *app = AppController::instance();
    return app ? app->outputHub() : nullptr;
}

void RecordingPanel::setOutputDir(const QString &dir)
{
    const QString trimmed = dir.trimmed();
    if (trimmed == m_outputDir)
        return;
    m_outputDir = trimmed;
    if (auto *app = AppController::instance())
        app->setOutputDir(m_outputDir);
    emit outputDirChanged();
}

void RecordingPanel::rebuild()
{
    auto *app = AppController::instance();
    OutputHub *h = hub();
    if (!app || !h)
        return;

    if (!m_connected) {
        m_connected = true;
        connect(h, &OutputHub::recordingStateChanged, this, &RecordingPanel::updateStates);
        connect(app, &AppController::outputDirChanged, this, [this] {
            const QString dir = RecordingOptions::loadSaved().effectiveOutputDir();
            if (dir != m_outputDir) {
                m_outputDir = dir;
                emit outputDirChanged();
            }
        });
    }

    const QString dir = RecordingOptions::loadSaved().effectiveOutputDir();
    if (dir != m_outputDir) {
        m_outputDir = dir;
        emit outputDirChanged();
    }

    ClipGraph *graph = app->clipGraph();
    m_video.clear();
    m_audio.clear();

    m_video.append(Row{tr("Program mix"), OutputHub::TrackKind::Program});
    m_video.append(Row{tr("Deck A (iso)"), OutputHub::TrackKind::DeckA});
    m_video.append(Row{tr("Deck B (iso)"), OutputHub::TrackKind::DeckB});
    m_audio.append(Row{tr("Program audio"), OutputHub::TrackKind::ProgramAudio});
    m_audio.append(Row{tr("Deck A audio (iso)"), OutputHub::TrackKind::DeckAAudio});
    m_audio.append(Row{tr("Deck B audio (iso)"), OutputHub::TrackKind::DeckBAudio});

    for (NodeId id : graph->nodeIdsOfType(ClipGraph::NodeType::Input)) {
        const SourceDescriptor desc = graph->sourceDescriptor(id);
        if (desc.isLiveSource())
            m_video.append(Row{graph->sourceName(id), OutputHub::TrackKind::Source, id});
        if (desc.kind == SourceDescriptor::Kind::VideoFile
            && hasStream(MediaProbe::probe(desc.path), StreamInfo::Type::Audio))
            m_audio.append(Row{graph->sourceName(id), OutputHub::TrackKind::ClipAudio, id});
    }

    updateStates();
}

void RecordingPanel::refreshStates(QVector<Row> &rows) const
{
    OutputHub *h = hub();
    if (!h)
        return;
    for (Row &row : rows) {
        row.active = h->isTrackRecording(row.kind, row.nodeId);
        row.elapsedMs = row.active ? h->trackRecordingDurationMs(row.kind, row.nodeId) : 0;
    }
}

void RecordingPanel::updateStates()
{
    refreshStates(m_video);
    refreshStates(m_audio);
    emit rowsChanged();
}

QVariantList RecordingPanel::rowsToVariant(const QVector<Row> &rows)
{
    QVariantList out;
    for (const Row &row : rows) {
        const qint64 secs = row.elapsedMs / 1000;
        const QString elapsed = QStringLiteral("%1:%2:%3")
            .arg(secs / 3600, 2, 10, QChar('0'))
            .arg((secs % 3600) / 60, 2, 10, QChar('0'))
            .arg(secs % 60, 2, 10, QChar('0'));
        out.append(QVariantMap{{"label", row.label}, {"active", row.active}, {"elapsed", elapsed}});
    }
    return out;
}

void RecordingPanel::toggle(bool audio, int index, bool on)
{
    OutputHub *h = hub();
    QVector<Row> &rows = audio ? m_audio : m_video;
    if (!h || index < 0 || index >= rows.size())
        return;
    const Row &row = rows.at(index);

    h->setOutputDir(m_outputDir);
    if (on) {
        switch (row.kind) {
        case OutputHub::TrackKind::Program:      h->startProgramRecording(); break;
        case OutputHub::TrackKind::DeckA:        h->startDeckARecording(); break;
        case OutputHub::TrackKind::DeckB:        h->startDeckBRecording(); break;
        case OutputHub::TrackKind::Source:       h->startSourceRecording(row.nodeId, row.label); break;
        case OutputHub::TrackKind::ProgramAudio: h->startProgramAudioRecording(); break;
        case OutputHub::TrackKind::DeckAAudio:   h->startDeckAAudioRecording(); break;
        case OutputHub::TrackKind::DeckBAudio:   h->startDeckBAudioRecording(); break;
        case OutputHub::TrackKind::ClipAudio:    h->startClipAudioRecording(row.nodeId, row.label); break;
        }
    } else {
        switch (row.kind) {
        case OutputHub::TrackKind::Program:      h->stopProgramRecording(); break;
        case OutputHub::TrackKind::DeckA:        h->stopDeckARecording(); break;
        case OutputHub::TrackKind::DeckB:        h->stopDeckBRecording(); break;
        case OutputHub::TrackKind::Source:       h->stopSourceRecording(row.nodeId); break;
        case OutputHub::TrackKind::ProgramAudio: h->stopProgramAudioRecording(); break;
        case OutputHub::TrackKind::DeckAAudio:   h->stopDeckAAudioRecording(); break;
        case OutputHub::TrackKind::DeckBAudio:   h->stopDeckBAudioRecording(); break;
        case OutputHub::TrackKind::ClipAudio:    h->stopClipAudioRecording(row.nodeId); break;
        }
    }
    updateStates();
}
