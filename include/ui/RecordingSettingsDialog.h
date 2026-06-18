#pragma once

#include <QDialog>
#include "ui/RecordingOptions.h"

class ClipNodeEditor;
class QCheckBox;
class QLineEdit;
class QVBoxLayout;

class RecordingSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit RecordingSettingsDialog(ClipNodeEditor *editor, QWidget *parent = nullptr);

    RecordingOptions options() const;
    void setOptions(const RecordingOptions &opts);

    static RecordingOptions loadSavedOptions();
    static void saveOptions(const RecordingOptions &opts);

private:
    void rebuildSourceList();
    void browseOutputDir();

    ClipNodeEditor *m_editor = nullptr;
    QCheckBox      *m_programCheck = nullptr;
    QCheckBox      *m_deckACheck   = nullptr;
    QCheckBox      *m_deckBCheck   = nullptr;
    QLineEdit      *m_outputDirEdit = nullptr;
    QVBoxLayout    *m_sourceListLayout = nullptr;
    QVector<QCheckBox *> m_sourceChecks;
    QVector<NodeId>      m_sourceNodeIds;
};
