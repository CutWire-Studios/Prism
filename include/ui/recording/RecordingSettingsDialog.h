#pragma once

#include <QDialog>
#include <QVector>
#include "ui/recording/RecordingOptions.h"
#include "app/output/OutputHub.h"
#include "core/graph/ClipGraph.h"

class QVBoxLayout;
class QLineEdit;
class QTimer;
class QLabel;
class QPushButton;

/// Modeless panel for starting/stopping each recording stream independently.
class RecordingSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit RecordingSettingsDialog(OutputHub *hub, ClipGraph *graph, QWidget *parent = nullptr);

    void syncFromHub();

    static RecordingOptions loadSavedOptions();
    static void saveOutputDir(const QString &dir);
    /// True once the user has explicitly picked an output folder.
    static bool hasChosenOutputDir();

private slots:
    void refreshTrackUi();
    void browseOutputDir();
    void onOutputDirEdited();
    void onTrackToggled(bool on);

private:
    struct StreamRow {
        QLabel     *timeLabel  = nullptr;
        QPushButton *toggleBtn = nullptr;
        OutputHub::TrackKind kind = OutputHub::TrackKind::Program;
        NodeId      nodeId = 0;
        QString     label;
    };

    void rebuildStreamRows();
    void rebuildAudioRows();
    StreamRow *rowForSender();

    OutputHub       *m_hub = nullptr;
    ClipGraph       *m_graph = nullptr;
    QLineEdit       *m_outputDirEdit = nullptr;
    QVBoxLayout     *m_streamListLayout = nullptr;
    QVBoxLayout     *m_audioListLayout = nullptr;
    QTimer          *m_uiTimer = nullptr;
    QVector<StreamRow> m_rows;
    QVector<StreamRow> m_audioRows;
};
