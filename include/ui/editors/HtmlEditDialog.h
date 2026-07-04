#pragma once

#include <QColor>
#include <QDialog>
#include <QJsonObject>
#include <QList>
#include <QString>
#include "core/sources/HtmlWorkspace.h"

namespace Ui { class HtmlEditDialog; }
class QWebEngineView;
class QListWidgetItem;
class QPushButton;

/// Editor for an HTML source in two modes: Simple (raw HTML or a file, with
/// Code and Visual views — the Visual view edits the document WYSIWYG without
/// touching code) and Workspace (WYSIWYG canvas where bundled widget presets
/// render live and are arranged by drag and drop). Returns the result for
/// whichever mode is active.
class HtmlEditDialog : public QDialog {
    Q_OBJECT

public:
    enum class EditMode { Simple, Workspace };

    explicit HtmlEditDialog(const QString &initialHtml = {},
                            const QString &initialWorkspaceJson = {},
                            QWidget *parent = nullptr);
    ~HtmlEditDialog() override;

    EditMode editMode() const;

    QString resultHtml() const;
    QString resultFilePath() const;
    QString resultWorkspaceJson() const;
    QString resultBakedHtml() const;

private slots:
    void onPresetSelected(int row);
    void onBrowse();
    void onClearFile();
    void onRefreshSimplePreview();
    void onWorkspaceChanged();
    void onComponentSelected(int index);
    void onPropSpinChanged();
    void onDeleteComponent();
    void onDuplicateComponent();
    void onLayerRowChanged(int row);
    void onLayerItemChanged(QListWidgetItem *item);
    void onLayerMoveUp();
    void onLayerMoveDown();
    void onSimpleViewChanged(bool visual);
    void onVisualHtmlEdited();
    void onVisualSelection(const QJsonObject &info);
    void onInspFontSizeChanged(int px);
    void onInspOpacityChanged(int percent);
    void onInspTextColor();
    void onInspBgColor();

private:
    void loadSimplePreview(const QString &html, const QString &filePath = {});
    void syncPropsFromSelection();
    void applyPropsToSelection();
    void rebuildLayerList();
    void moveSelectedLayer(int delta);
    void enterVisualView();
    void syncVisualToCode();
    void setColorSwatch(QPushButton *btn, const QColor &color);
    HtmlWorkspace currentWorkspace() const;

    Ui::HtmlEditDialog *ui;
    QWebEngineView     *m_simplePreview = nullptr;
    bool                m_propChanging  = false;
    bool                m_layerSyncing  = false;
    bool                m_inspChanging  = false;
    QColor              m_inspTextColor;
    QColor              m_inspBgColor;
    QList<int>          m_layerRowToComp;
    QString             m_layerSignature;
};
