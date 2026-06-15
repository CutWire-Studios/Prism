#include "ui/HtmlEditDialog.h"
#include "ui_HtmlEditDialog.h"
#include <QWebEngineView>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QFile>
#include <QFont>
#include <QUrl>
#include <QFileInfo>

namespace {

struct Preset { const char *name; const char *resource; };

static const Preset kPresets[] = {
    { "Clock",               ":/html/clock.html"               },
    { "Countdown Timer",     ":/html/countdown_timer.html"     },
    { "Cricket Score Bar",   ":/html/cricket_score_bar.html"   },
    { "Cricket Score Table", ":/html/cricket_score_table.html" },
};
static const int kPresetCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

static QString loadResource(const char *path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

} // namespace

HtmlEditDialog::HtmlEditDialog(const QString &initialHtml, QWidget *parent)
    : QDialog(parent), ui(new Ui::HtmlEditDialog)
{
    ui->setupUi(this);
    resize(1100, 680);

    // Code editor font
    QFont mono("Monospace", 9);
    mono.setStyleHint(QFont::Monospace);
    mono.setFixedPitch(true);
    ui->codeEdit->setFont(mono);
    ui->codeEdit->setTabStopDistance(28);

    // Embed WebEngineView into the preview container
    m_preview = new QWebEngineView(ui->previewContainer);
    auto *lay = new QVBoxLayout(ui->previewContainer);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(m_preview);
    m_preview->setFixedSize(ui->previewContainer->minimumSize());

    // Populate preset list
    for (int i = 0; i < kPresetCount; ++i)
        ui->presetList->addItem(kPresets[i].name);
    ui->presetList->addItem("Custom HTML");

    connect(ui->presetList,  &QListWidget::currentRowChanged,
            this,            &HtmlEditDialog::onPresetSelected);
    connect(ui->browseBtn,   &QPushButton::clicked, this, &HtmlEditDialog::onBrowse);
    connect(ui->clearFileBtn,&QPushButton::clicked, this, &HtmlEditDialog::onClearFile);
    connect(ui->refreshBtn,  &QPushButton::clicked, this, &HtmlEditDialog::onRefresh);
    connect(ui->buttonBox,   &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox,   &QDialogButtonBox::rejected, this, &QDialog::reject);

    if (!initialHtml.isEmpty()) {
        ui->codeEdit->setPlainText(initialHtml);
        // Try to match a preset
        int matchRow = kPresetCount;
        for (int i = 0; i < kPresetCount; ++i) {
            if (loadResource(kPresets[i].resource) == initialHtml.trimmed()) {
                matchRow = i;
                break;
            }
        }
        ui->presetList->blockSignals(true);
        ui->presetList->setCurrentRow(matchRow);
        ui->presetList->blockSignals(false);
        loadPreview(initialHtml);
    } else {
        ui->presetList->setCurrentRow(0); // triggers onPresetSelected
    }
}

HtmlEditDialog::~HtmlEditDialog() {
    delete ui;
}

QString HtmlEditDialog::resultHtml() const {
    if (!ui->filePathEdit->text().isEmpty()) return {};
    return ui->codeEdit->toPlainText();
}

QString HtmlEditDialog::resultFilePath() const {
    return ui->filePathEdit->text().trimmed();
}

void HtmlEditDialog::onPresetSelected(int row) {
    if (row < 0 || row >= kPresetCount) return;
    ui->filePathEdit->clear();
    QString html = loadResource(kPresets[row].resource);
    if (!html.isEmpty()) {
        ui->codeEdit->setPlainText(html);
        loadPreview(html);
    }
}

void HtmlEditDialog::onBrowse() {
    QString path = QFileDialog::getOpenFileName(
        this, "Open HTML File", {},
        "HTML Files (*.html *.htm);;All Files (*)");
    if (path.isEmpty()) return;

    ui->filePathEdit->setText(path);
    ui->presetList->blockSignals(true);
    ui->presetList->setCurrentRow(kPresetCount); // "Custom HTML"
    ui->presetList->blockSignals(false);

    QFile f(path);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text))
        ui->codeEdit->setPlainText(QString::fromUtf8(f.readAll()));

    loadPreview({}, path);
}

void HtmlEditDialog::onClearFile() {
    ui->filePathEdit->clear();
    ui->errorLabel->clear();
}

void HtmlEditDialog::onRefresh() {
    const QString fp = ui->filePathEdit->text().trimmed();
    if (!fp.isEmpty())
        loadPreview({}, fp);
    else
        loadPreview(ui->codeEdit->toPlainText());
}

void HtmlEditDialog::loadPreview(const QString &html, const QString &filePath) {
    ui->errorLabel->clear();
    if (!filePath.isEmpty()) {
        m_preview->load(QUrl::fromLocalFile(filePath));
    } else {
        m_preview->setHtml(html, QUrl("qrc:/"));
    }
}
