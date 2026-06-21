#pragma once

#include "core/scripting/ScriptRuntime.h"
#include <QDialog>
#include <memory>

namespace Ui { class ScriptEditDialog; }

/// Editor for a Lua script node: preset or custom code, trigger mode + interval,
/// and a run-now preview. Returns code and trigger config via result*() getters.
/// (Shares the preset-list + code-editor + preview shape with ShaderEditDialog,
/// but the preset data and preview backends differ enough to keep them separate.)
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
