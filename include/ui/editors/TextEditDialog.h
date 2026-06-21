#pragma once

#include "core/sources/SourceDescriptor.h"
#include <QColor>
#include <QDialog>

namespace Ui { class TextEditDialog; }

/// Editor for a Text source: content template, font, text/background colors and
/// alignment. Reads from and returns a SourceDescriptor.
class TextEditDialog : public QDialog {
    Q_OBJECT
public:
    explicit TextEditDialog(const SourceDescriptor &initial = SourceDescriptor(),
                            QWidget *parent = nullptr);
    ~TextEditDialog() override;

    SourceDescriptor resultDescriptor() const;

private slots:
    void onPickTextColor();
    void onPickBgColor();
    void onBgTransparentToggled(bool transparent);

private:
    void setFromDescriptor(const SourceDescriptor &desc);
    void updateColorButtons();
    static int alignmentToIndex(int textAlign);

    Ui::TextEditDialog *ui;
    QColor m_textColor = Qt::white;
    QColor m_bgColor   = Qt::black;
};
