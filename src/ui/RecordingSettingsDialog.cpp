#include "ui/RecordingSettingsDialog.h"
#include "ui/ClipNodeEditor.h"
#include "ui/ProgramRecorder.h"
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QSettings>
#include <QSet>

namespace {
constexpr char kSettingsGroup[] = "Recording";
constexpr char kProgramKey[] = "recordProgram";
constexpr char kDeckAKey[] = "recordDeckA";
constexpr char kDeckBKey[] = "recordDeckB";
constexpr char kOutputDirKey[] = "outputDir";
constexpr char kSourceNamesKey[] = "sourceNames";
}

RecordingSettingsDialog::RecordingSettingsDialog(ClipNodeEditor *editor, QWidget *parent)
    : QDialog(parent)
    , m_editor(editor)
{
    setWindowTitle(tr("Recording Settings"));
    setMinimumWidth(420);

    auto *mainLayout = new QVBoxLayout(this);

    auto *tracksGroup = new QGroupBox(tr("What to record"), this);
    auto *tracksLayout = new QVBoxLayout(tracksGroup);
    m_programCheck = new QCheckBox(tr("Program mix (crossfaded output)"), this);
    m_deckACheck   = new QCheckBox(tr("Deck A (iso, pre-fader)"), this);
    m_deckBCheck   = new QCheckBox(tr("Deck B (iso, pre-fader)"), this);
    m_programCheck->setChecked(true);
    tracksLayout->addWidget(m_programCheck);
    tracksLayout->addWidget(m_deckACheck);
    tracksLayout->addWidget(m_deckBCheck);
    mainLayout->addWidget(tracksGroup);

    auto *sourcesGroup = new QGroupBox(tr("Individual live sources"), this);
    auto *sourcesOuter = new QVBoxLayout(sourcesGroup);
    auto *sourcesHint = new QLabel(
        tr("Record while the source is loaded on Deck A or B. No frames are written when it is off-air."),
        this);
    sourcesHint->setWordWrap(true);
    sourcesHint->setStyleSheet(QStringLiteral("color: #888; font-size: 11px;"));
    sourcesOuter->addWidget(sourcesHint);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setMaximumHeight(160);
    auto *sourcesWidget = new QWidget(scroll);
    m_sourceListLayout = new QVBoxLayout(sourcesWidget);
    m_sourceListLayout->setContentsMargins(0, 0, 0, 0);
    scroll->setWidget(sourcesWidget);
    sourcesOuter->addWidget(scroll);
    mainLayout->addWidget(sourcesGroup);

    auto *dirGroup = new QGroupBox(tr("Output folder"), this);
    auto *dirLayout = new QHBoxLayout(dirGroup);
    m_outputDirEdit = new QLineEdit(this);
    auto *browseBtn = new QPushButton(tr("Browse…"), this);
    connect(browseBtn, &QPushButton::clicked, this, &RecordingSettingsDialog::browseOutputDir);
    dirLayout->addWidget(m_outputDirEdit, 1);
    dirLayout->addWidget(browseBtn);
    mainLayout->addWidget(dirGroup);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    rebuildSourceList();
}

void RecordingSettingsDialog::rebuildSourceList() {
    while (QLayoutItem *item = m_sourceListLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    m_sourceChecks.clear();
    m_sourceNodeIds.clear();

    if (!m_editor) {
        m_sourceListLayout->addWidget(new QLabel(tr("No clip graph loaded."), this));
        return;
    }

    bool anyLive = false;
    for (ClipNodeModel *node : m_editor->allNodes()) {
        if (!node || !node->hasSource() || !node->isLiveSource())
            continue;
        anyLive = true;
        auto *check = new QCheckBox(node->sourceName(), this);
        m_sourceListLayout->addWidget(check);
        m_sourceChecks.append(check);
        m_sourceNodeIds.append(node->nodeId());
    }

    if (!anyLive)
        m_sourceListLayout->addWidget(new QLabel(tr("No live sources in the clip graph."), this));
}

RecordingOptions RecordingSettingsDialog::options() const {
    RecordingOptions opts;
    opts.recordProgram = m_programCheck->isChecked();
    opts.recordDeckA   = m_deckACheck->isChecked();
    opts.recordDeckB   = m_deckBCheck->isChecked();
    opts.outputDir     = m_outputDirEdit->text().trimmed();

    for (int i = 0; i < m_sourceChecks.size(); ++i) {
        if (!m_sourceChecks[i]->isChecked())
            continue;
        RecordingSourceTarget target;
        target.nodeId = m_sourceNodeIds[i];
        target.label  = m_sourceChecks[i]->text();
        opts.recordSources.append(target);
    }
    return opts;
}

void RecordingSettingsDialog::setOptions(const RecordingOptions &opts) {
    m_programCheck->setChecked(opts.recordProgram);
    m_deckACheck->setChecked(opts.recordDeckA);
    m_deckBCheck->setChecked(opts.recordDeckB);
    m_outputDirEdit->setText(opts.outputDir.isEmpty()
        ? ProgramRecorder::defaultOutputDir()
        : opts.outputDir);

    QSet<QString> selected;
    for (const RecordingSourceTarget &target : opts.recordSources)
        selected.insert(target.label);

    for (int i = 0; i < m_sourceChecks.size(); ++i)
        m_sourceChecks[i]->setChecked(selected.contains(m_sourceChecks[i]->text()));
}

RecordingOptions RecordingSettingsDialog::loadSavedOptions() {
    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));

    RecordingOptions opts;
    opts.recordProgram = settings.value(QLatin1String(kProgramKey), true).toBool();
    opts.recordDeckA   = settings.value(QLatin1String(kDeckAKey), false).toBool();
    opts.recordDeckB   = settings.value(QLatin1String(kDeckBKey), false).toBool();
    opts.outputDir     = settings.value(QLatin1String(kOutputDirKey), ProgramRecorder::defaultOutputDir()).toString();

    const QStringList sourceNames = settings.value(QLatin1String(kSourceNamesKey)).toStringList();
    for (const QString &name : sourceNames) {
        RecordingSourceTarget target;
        target.label = name;
        opts.recordSources.append(target);
    }

    settings.endGroup();
    return opts;
}

void RecordingSettingsDialog::saveOptions(const RecordingOptions &opts) {
    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    settings.setValue(QLatin1String(kProgramKey), opts.recordProgram);
    settings.setValue(QLatin1String(kDeckAKey), opts.recordDeckA);
    settings.setValue(QLatin1String(kDeckBKey), opts.recordDeckB);
    settings.setValue(QLatin1String(kOutputDirKey),
                      opts.outputDir.isEmpty() ? ProgramRecorder::defaultOutputDir() : opts.outputDir);

    QStringList sourceNames;
    for (const RecordingSourceTarget &target : opts.recordSources)
        sourceNames << target.label;
    settings.setValue(QLatin1String(kSourceNamesKey), sourceNames);
    settings.endGroup();
}

void RecordingSettingsDialog::browseOutputDir() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Recording Output Folder"), m_outputDirEdit->text());
    if (!dir.isEmpty())
        m_outputDirEdit->setText(dir);
}
