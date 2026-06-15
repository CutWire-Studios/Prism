#pragma once

#include <QDialog>
#include <QString>
#include <memory>

namespace Ui { class HtmlEditDialog; }
class QWebEngineView;

class HtmlEditDialog : public QDialog {
    Q_OBJECT

public:
    explicit HtmlEditDialog(const QString &initialHtml = {}, QWidget *parent = nullptr);
    ~HtmlEditDialog() override;

    // Returns inline HTML (or empty string if a file was selected).
    QString resultHtml() const;
    // Returns selected file path (empty if using inline HTML).
    QString resultFilePath() const;

private slots:
    void onPresetSelected(int row);
    void onBrowse();
    void onClearFile();
    void onRefresh();

private:
    void loadPreview(const QString &html, const QString &filePath = {});

    Ui::HtmlEditDialog *ui;
    QWebEngineView     *m_preview = nullptr;
};
