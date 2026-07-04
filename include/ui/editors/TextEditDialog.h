#pragma once

#include "core/scripting/ScriptOutput.h"
#include "core/sources/SourceDescriptor.h"
#include <QColor>
#include <QDialog>
#include <QJsonObject>

namespace Ui { class TextEditDialog; }
class QButtonGroup;
class QTimer;

/// Editor for a Text source: template with {placeholder} tokens, typography,
/// alignment/justification, fill (solid or gradient), outline, shadow and
/// background — with a live preview rendered by the same code as the program
/// output. When the text node's DataIn port is wired to a Script node, the
/// script's variables appear as draggable blocks with live values.
class TextEditDialog : public QDialog {
    Q_OBJECT
public:
    explicit TextEditDialog(const SourceDescriptor &initial = SourceDescriptor(),
                            QWidget *parent = nullptr);
    ~TextEditDialog() override;

    /// Attach the Script node wired to this text node's DataIn port (if any).
    void setScriptBinding(const ScriptBinding &binding);

    SourceDescriptor resultDescriptor() const;

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void updatePreview();
    void onPollScriptOutput();
    void tryAccept();

private:
    class Highlighter;

    void setFromDescriptor(const SourceDescriptor &desc);
    SourceDescriptor currentDescriptor() const;
    void pickColor(QColor &target, const QString &title);
    void updateColorButtons();
    void updateFillControls();
    void rebuildVariableChips();
    void insertToken(const QString &name);
    QStringList knownVariableNames() const;

    Ui::TextEditDialog *ui;
    QColor m_textColor    = Qt::white;
    QColor m_textColor2   = QColor(0x00, 0xbf, 0xff);
    QColor m_outlineColor = Qt::black;
    QColor m_shadowColor  = QColor(0, 0, 0, 160);
    QColor m_bgColor      = Qt::black;
    int m_canvasW = 1280;
    int m_canvasH = 720;

    QButtonGroup *m_hAlignGroup = nullptr;
    QButtonGroup *m_vAlignGroup = nullptr;
    Highlighter *m_highlighter = nullptr;

    ScriptBinding m_binding;
    QJsonObject m_vars;              // latest values published by the script
    QStringList m_declaredVars;      // keys parsed from the Lua source
    QStringList m_chipKeys;          // keys the current chips were built for
    QList<QWidget *> m_chipWidgets;
    bool m_chipStretchAdded = false;
    uint m_lastSeenVersion = 0;
    QTimer *m_pollTimer = nullptr;
};
