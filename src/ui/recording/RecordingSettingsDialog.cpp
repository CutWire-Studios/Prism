#include "ui/recording/RecordingSettingsDialog.h"
#include "ui/nodes/ClipNodeEditor.h"
#include "ui/recording/ProgramRecorder.h"
#include "core/media/VideoPlayer.h"
#include "core/sources/SourceDescriptor.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QFileDialog>
#include <QSettings>
#include <QTimer>

namespace {
constexpr char kSettingsGroup[] = "Recording";
constexpr char kOutputDirKey[] = "outputDir";

QString formatElapsed(qint64 ms) {
    const qint64 totalSecs = ms / 1000;
    const int hours   = static_cast<int>(totalSecs / 3600);
    const int minutes = static_cast<int>((totalSecs % 3600) / 60);
    const int seconds = static_cast<int>(totalSecs % 60);
    return QStringLiteral("%1:%2:%3")
        .arg(hours,   2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

QWidget *makeStreamRowWidget(const QString &title, QLabel *&timeLabel, QPushButton *&toggleBtn,
                             QWidget *parent) {
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 2, 0, 2);

    auto *nameLabel = new QLabel(title, row);
    nameLabel->setMinimumWidth(160);
    timeLabel = new QLabel(QStringLiteral("00:00:00"), row);
    timeLabel->setMinimumWidth(64);
    timeLabel->setStyleSheet(QStringLiteral("color: #888; font-family: monospace;"));

    toggleBtn = new QPushButton(QObject::tr("Record"), row);
    toggleBtn->setCheckable(true);
    toggleBtn->setMinimumWidth(72);

    layout->addWidget(nameLabel, 1);
    layout->addWidget(timeLabel);
    layout->addWidget(toggleBtn);
    return row;
}
}

RecordingSettingsDialog::RecordingSettingsDialog(OutputHub *hub, ClipNodeEditor *editor, QWidget *parent)
    : QDialog(parent)
    , m_hub(hub)
    , m_editor(editor)
    , m_uiTimer(new QTimer(this))
{
    setWindowTitle(tr("Recording Panel"));
    setMinimumWidth(460);
    setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint);

    auto *mainLayout = new QVBoxLayout(this);

    auto *hint = new QLabel(
        tr("Start and stop each stream independently. Tip: start Program mix and Program audio "
           "at the same time so the files are easy to match in your editor."),
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #888; font-size: 11px;"));
    mainLayout->addWidget(hint);

    auto *streamsGroup = new QGroupBox(tr("Recording streams"), this);
    auto *streamsOuter = new QVBoxLayout(streamsGroup);

    auto *streamsHint = new QLabel(
        tr("Video saves as H.264 MKV (no audio). Live-source rows record only while that source "
           "is loaded on Deck A or B."),
        this);
    streamsHint->setWordWrap(true);
    streamsHint->setStyleSheet(QStringLiteral("color: #888; font-size: 11px;"));
    streamsOuter->addWidget(streamsHint);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setMinimumHeight(220);
    auto *streamsWidget = new QWidget(scroll);
    m_streamListLayout = new QVBoxLayout(streamsWidget);
    m_streamListLayout->setContentsMargins(0, 0, 0, 0);
    scroll->setWidget(streamsWidget);
    streamsOuter->addWidget(scroll);
    mainLayout->addWidget(streamsGroup, 1);

    auto *audioGroup = new QGroupBox(tr("Audio recording"), this);
    auto *audioOuter = new QVBoxLayout(audioGroup);
    auto *audioHint = new QLabel(
        tr("Saved as separate FLAC files (sync with video in your editor; automatic sync coming "
           "later). Program audio follows the crossfader and includes a connected Master Audio "
           "Input mic. Deck and clip isos record at full clip volume without the mic. Clip audio "
           "records only while that clip is loaded on Deck A or B."),
        this);
    audioHint->setWordWrap(true);
    audioHint->setStyleSheet(QStringLiteral("color: #888; font-size: 11px;"));
    audioOuter->addWidget(audioHint);
    auto *audioWidget = new QWidget(this);
    m_audioListLayout = new QVBoxLayout(audioWidget);
    m_audioListLayout->setContentsMargins(0, 0, 0, 0);
    audioOuter->addWidget(audioWidget);
    mainLayout->addWidget(audioGroup);

    auto *dirGroup = new QGroupBox(tr("Output folder"), this);
    auto *dirLayout = new QHBoxLayout(dirGroup);
    m_outputDirEdit = new QLineEdit(this);
    m_outputDirEdit->setText(RecordingSettingsDialog::loadSavedOptions().effectiveOutputDir());
    auto *browseBtn = new QPushButton(tr("Browse…"), this);
    connect(browseBtn, &QPushButton::clicked, this, &RecordingSettingsDialog::browseOutputDir);
    connect(m_outputDirEdit, &QLineEdit::editingFinished, this, &RecordingSettingsDialog::onOutputDirEdited);
    dirLayout->addWidget(m_outputDirEdit, 1);
    dirLayout->addWidget(browseBtn);
    mainLayout->addWidget(dirGroup);

    auto *closeBtn = new QPushButton(tr("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    mainLayout->addWidget(closeBtn, 0, Qt::AlignRight);

    rebuildStreamRows();
    rebuildAudioRows();

    m_uiTimer->setInterval(500);
    connect(m_uiTimer, &QTimer::timeout, this, &RecordingSettingsDialog::refreshTrackUi);

    if (m_hub) {
        connect(m_hub, &OutputHub::recordingStateChanged, this, &RecordingSettingsDialog::syncFromHub);
        connect(m_hub, &OutputHub::recordingProgress, this, [this](qint64) { refreshTrackUi(); });
        m_hub->setOutputDir(m_outputDirEdit->text().trimmed());
    }

    syncFromHub();
    m_uiTimer->start();
}

void RecordingSettingsDialog::rebuildStreamRows() {
    while (QLayoutItem *item = m_streamListLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    m_rows.clear();

    auto addRow = [&](const QString &label, OutputHub::TrackKind kind, NodeId nodeId = 0) {
        StreamRow row;
        row.kind  = kind;
        row.nodeId = nodeId;
        row.label = label;
        auto *widget = makeStreamRowWidget(label, row.timeLabel, row.toggleBtn, this);
        m_streamListLayout->addWidget(widget);
        connect(row.toggleBtn, &QPushButton::toggled, this, &RecordingSettingsDialog::onTrackToggled);
        m_rows.append(row);
    };

    addRow(tr("Program mix"), OutputHub::TrackKind::Program);
    addRow(tr("Deck A (iso)"), OutputHub::TrackKind::DeckA);
    addRow(tr("Deck B (iso)"), OutputHub::TrackKind::DeckB);

    if (m_editor) {
        for (ClipNodeModel *node : m_editor->allNodes()) {
            if (!node || !node->hasSource() || !node->isLiveSource())
                continue;
            addRow(node->sourceName(), OutputHub::TrackKind::Source, node->nodeId());
        }
    }

    if (m_rows.size() <= 3) {
        m_streamListLayout->addWidget(new QLabel(
            tr("Add live sources (camera, NDI, …) to the clip graph to record them here."), this));
    }

    m_streamListLayout->addStretch(1);
}

void RecordingSettingsDialog::rebuildAudioRows() {
    while (QLayoutItem *item = m_audioListLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    m_audioRows.clear();

    auto addRow = [&](const QString &label, OutputHub::TrackKind kind, NodeId nodeId = 0) {
        StreamRow row;
        row.kind   = kind;
        row.nodeId = nodeId;
        row.label  = label;
        auto *widget = makeStreamRowWidget(label, row.timeLabel, row.toggleBtn, this);
        m_audioListLayout->addWidget(widget);
        connect(row.toggleBtn, &QPushButton::toggled, this, &RecordingSettingsDialog::onTrackToggled);
        m_audioRows.append(row);
    };

    addRow(tr("Program audio"), OutputHub::TrackKind::ProgramAudio);
    addRow(tr("Deck A audio (iso)"), OutputHub::TrackKind::DeckAAudio);
    addRow(tr("Deck B audio (iso)"), OutputHub::TrackKind::DeckBAudio);

    if (m_editor) {
        for (ClipNodeModel *node : m_editor->allNodes()) {
            if (!node || !node->hasSource())
                continue;
            if (node->sourceDescriptor().kind != SourceDescriptor::Kind::VideoFile)
                continue;
            if (!VideoPlayer::fileHasAudio(node->sourceDescriptor().path))
                continue;
            addRow(node->sourceName(), OutputHub::TrackKind::ClipAudio, node->nodeId());
        }
    }
}

RecordingSettingsDialog::StreamRow *RecordingSettingsDialog::rowForSender() {
    auto *btn = qobject_cast<QPushButton *>(sender());
    if (!btn) return nullptr;
    for (StreamRow &row : m_rows) {
        if (row.toggleBtn == btn)
            return &row;
    }
    for (StreamRow &row : m_audioRows) {
        if (row.toggleBtn == btn)
            return &row;
    }
    return nullptr;
}

void RecordingSettingsDialog::onTrackToggled(bool on) {
    StreamRow *row = rowForSender();
    if (!row || !m_hub) return;

    onOutputDirEdited();

    if (on) {
        bool ok = false;
        switch (row->kind) {
        case OutputHub::TrackKind::Program:
            ok = m_hub->startProgramRecording();
            break;
        case OutputHub::TrackKind::DeckA:
            ok = m_hub->startDeckARecording();
            break;
        case OutputHub::TrackKind::DeckB:
            ok = m_hub->startDeckBRecording();
            break;
        case OutputHub::TrackKind::Source:
            ok = m_hub->startSourceRecording(row->nodeId, row->label);
            break;
        case OutputHub::TrackKind::ProgramAudio:
            ok = m_hub->startProgramAudioRecording();
            break;
        case OutputHub::TrackKind::DeckAAudio:
            ok = m_hub->startDeckAAudioRecording();
            break;
        case OutputHub::TrackKind::DeckBAudio:
            ok = m_hub->startDeckBAudioRecording();
            break;
        case OutputHub::TrackKind::ClipAudio:
            ok = m_hub->startClipAudioRecording(row->nodeId, row->label);
            break;
        default:
            break;
        }
        if (!ok) {
            row->toggleBtn->blockSignals(true);
            row->toggleBtn->setChecked(false);
            row->toggleBtn->blockSignals(false);
        }
    } else {
        switch (row->kind) {
        case OutputHub::TrackKind::Program:  m_hub->stopProgramRecording(); break;
        case OutputHub::TrackKind::DeckA:    m_hub->stopDeckARecording(); break;
        case OutputHub::TrackKind::DeckB:    m_hub->stopDeckBRecording(); break;
        case OutputHub::TrackKind::Source:   m_hub->stopSourceRecording(row->nodeId); break;
        case OutputHub::TrackKind::ProgramAudio: m_hub->stopProgramAudioRecording(); break;
        case OutputHub::TrackKind::DeckAAudio:  m_hub->stopDeckAAudioRecording(); break;
        case OutputHub::TrackKind::DeckBAudio:  m_hub->stopDeckBAudioRecording(); break;
        case OutputHub::TrackKind::ClipAudio:   m_hub->stopClipAudioRecording(row->nodeId); break;
        default:
            break;
        }
    }

    refreshTrackUi();
}

void RecordingSettingsDialog::syncFromHub() {
    if (!m_hub) return;

    for (StreamRow &row : m_rows) {
        const bool active = m_hub->isTrackRecording(row.kind, row.nodeId);
        row.toggleBtn->blockSignals(true);
        row.toggleBtn->setChecked(active);
        row.toggleBtn->setText(active ? tr("Stop") : tr("Record"));
        row.toggleBtn->blockSignals(false);
    }
    for (StreamRow &row : m_audioRows) {
        const bool active = m_hub->isTrackRecording(row.kind, row.nodeId);
        row.toggleBtn->blockSignals(true);
        row.toggleBtn->setChecked(active);
        row.toggleBtn->setText(active ? tr("Stop") : tr("Record"));
        row.toggleBtn->blockSignals(false);
    }
    refreshTrackUi();
}

void RecordingSettingsDialog::refreshTrackUi() {
    if (!m_hub) return;

    for (StreamRow &row : m_rows) {
        const bool active = m_hub->isTrackRecording(row.kind, row.nodeId);
        row.toggleBtn->setText(active ? tr("Stop") : tr("Record"));
        if (active) {
            const qint64 ms = m_hub->trackRecordingDurationMs(row.kind, row.nodeId);
            row.timeLabel->setText(formatElapsed(ms));
            row.timeLabel->setStyleSheet(QStringLiteral("color: #e04545; font-family: monospace;"));
        } else {
            row.timeLabel->setText(QStringLiteral("00:00:00"));
            row.timeLabel->setStyleSheet(QStringLiteral("color: #888; font-family: monospace;"));
        }
    }
    for (StreamRow &row : m_audioRows) {
        const bool active = m_hub->isTrackRecording(row.kind, row.nodeId);
        row.toggleBtn->setText(active ? tr("Stop") : tr("Record"));
        if (active) {
            const qint64 ms = m_hub->trackRecordingDurationMs(row.kind, row.nodeId);
            row.timeLabel->setText(formatElapsed(ms));
            row.timeLabel->setStyleSheet(QStringLiteral("color: #e04545; font-family: monospace;"));
        } else {
            row.timeLabel->setText(QStringLiteral("00:00:00"));
            row.timeLabel->setStyleSheet(QStringLiteral("color: #888; font-family: monospace;"));
        }
    }
}

RecordingOptions RecordingSettingsDialog::loadSavedOptions() {
    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    RecordingOptions opts;
    opts.outputDir = settings.value(QLatin1String(kOutputDirKey), ProgramRecorder::defaultOutputDir()).toString();
    settings.endGroup();
    return opts;
}

void RecordingSettingsDialog::saveOutputDir(const QString &dir) {
    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    settings.setValue(QLatin1String(kOutputDirKey),
                      dir.isEmpty() ? ProgramRecorder::defaultOutputDir() : dir);
    settings.endGroup();
}

void RecordingSettingsDialog::browseOutputDir() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Recording Output Folder"), m_outputDirEdit->text());
    if (!dir.isEmpty()) {
        m_outputDirEdit->setText(dir);
        onOutputDirEdited();
    }
}

void RecordingSettingsDialog::onOutputDirEdited() {
    const QString dir = m_outputDirEdit->text().trimmed();
    if (m_hub)
        m_hub->setOutputDir(dir);
    saveOutputDir(dir);
}
