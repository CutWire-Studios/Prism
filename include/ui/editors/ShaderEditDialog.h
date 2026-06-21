#pragma once
#include <QDialog>
#include <memory>

namespace Ui { class ShaderEditDialog; }
class ShaderSource;

/// Editor for a GLSL shader source: pick a bundled preset or write custom code,
/// with a live compiled preview. Returns the chosen code via resultCode().
class ShaderEditDialog : public QDialog {
    Q_OBJECT
public:
    explicit ShaderEditDialog(const QString &initialCode = QString(),
                              QWidget *parent = nullptr);
    ~ShaderEditDialog() override;

    QString resultCode() const;

private slots:
    void onPresetSelected(int row);
    void onCompilePreview();

private:
    void updatePreview();

    Ui::ShaderEditDialog         *ui;
    std::unique_ptr<ShaderSource> m_previewSrc;
};
