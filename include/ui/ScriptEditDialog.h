#pragma once

#include "core/ScriptRuntime.h"
#include <QDialog>
#include <memory>

namespace Ui { class ScriptEditDialog; }

class ScriptEditDialog : public QDialog {
    Q_OBJECT
public:
    explicit ScriptEditDialog(const QString &initialCode = QString(),
                              ScriptTriggerMode trigger = ScriptTriggerMode::Periodic,
                              int intervalMs = 1000,
                              QWidget *parent = nullptr);
    ~ScriptEditDialog() override;

    QString resultCode() const;
    ScriptTriggerMode resultTriggerMode() const;
    int resultIntervalMs() const;

private slots:
    void onPresetSelected(int row);
    void onRunNow();
    void onTriggerChanged(int index);

private:
    void applyPreset(int row);
    void runPreview();
    void updateTriggerControls();

    Ui::ScriptEditDialog *ui;
    std::unique_ptr<ScriptRuntime> m_previewRuntime;
    std::shared_ptr<ScriptOutput> m_previewOutput;
};
