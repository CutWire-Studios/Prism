#pragma once

#include "core/sources/SourceDescriptor.h"
#include "core/sources/SvgTemplates.h"
#include <QDialog>
#include <QJsonObject>

namespace Ui { class SvgTemplateDialog; }

/// Editor for an SVG Template source: pick a built-in template or open a custom SVG file, fill in
/// its parameters, and see the result in a live preview rendered by the same code as program output.
class SvgTemplateDialog : public QDialog {
    Q_OBJECT
public:
    explicit SvgTemplateDialog(const SourceDescriptor &initial = SourceDescriptor(),
                               QWidget *parent = nullptr);
    ~SvgTemplateDialog() override;

    SourceDescriptor resultDescriptor() const;

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void selectTemplate(const QString &id);
    void addCustomItem(const QString &path);
    void rebuildParams();
    void setValue(const QString &name, const QString &value);
    void pickColor(const QString &name);
    void updatePreview();
    SourceDescriptor currentDescriptor() const;

    Ui::SvgTemplateDialog *ui;
    prism::SvgTemplateInfo m_info;
    QJsonObject m_overrides;
    bool m_building = false;
};
