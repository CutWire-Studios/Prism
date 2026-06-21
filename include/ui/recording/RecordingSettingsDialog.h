#pragma once

#include <QDialog>
#include <QVector>
#include "ui/recording/RecordingOptions.h"
#include "ui/output/OutputHub.h"
#include "ui/nodes/ClipNodeModel.h"

class ClipNodeEditor;
class QVBoxLayout;
class QLineEdit;
class QTimer;
class QLabel;
class QPushButton;

/// Modeless panel for starting/stopping each recording stream independently.
class RecordingSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit RecordingSettingsDialog(OutputHub *hub, ClipNodeEditor *editor, QWidget *parent = nullptr);

    void syncFromHub();

    static RecordingOptions loadSavedOptions();
    static void saveOutputDir(const QString &dir);

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
    StreamRow *rowForSender();

    OutputHub       *m_hub = nullptr;
    ClipNodeEditor  *m_editor = nullptr;
    QLineEdit       *m_outputDirEdit = nullptr;
    QVBoxLayout     *m_streamListLayout = nullptr;
    QTimer          *m_uiTimer = nullptr;
    QVector<StreamRow> m_rows;
};
