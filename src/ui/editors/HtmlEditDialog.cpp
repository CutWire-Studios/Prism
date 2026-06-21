#include "ui/editors/HtmlEditDialog.h"
#include "ui_HtmlEditDialog.h"
#include "core/sources/HtmlWorkspace.h"
#include <algorithm>
#include <QWebEngineView>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QFile>
#include <QFont>
#include <QUrl>
#include <QFileInfo>
#include <QListWidget>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QApplication>
#include <QUuid>

namespace {

constexpr char kPresetMime[] = "application/x-switchx-html-preset";

struct LegacyPreset { const char *name; const char *resource; };

static const LegacyPreset kLegacyPresets[] = {
    { "Clock",               ":/html/clock.html"               },
    { "Countdown Timer",     ":/html/countdown_timer.html"     },
    { "Cricket Score Bar",   ":/html/cricket_score_bar.html"   },
    { "Cricket Score Table", ":/html/cricket_score_table.html" },
};
static const int kLegacyPresetCount =
    static_cast<int>(sizeof(kLegacyPresets) / sizeof(kLegacyPresets[0]));

static QString loadResource(const char *path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

class DraggablePaletteWidget : public QListWidget {
public:
    using QListWidget::QListWidget;

protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton)
            m_dragStart = e->position();
        QListWidget::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        if (!(e->buttons() & Qt::LeftButton) || !currentItem())
            return;
        if ((e->position() - m_dragStart).manhattanLength()
            < QApplication::startDragDistance())
            return;

        auto *mime = new QMimeData;
        mime->setData(kPresetMime,
                      currentItem()->data(Qt::UserRole).toString().toUtf8());
        auto *drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(Qt::CopyAction);
    }

private:
    QPointF m_dragStart;
};

} // namespace

HtmlEditDialog::HtmlEditDialog(const QString &initialHtml,
                               const QString &initialWorkspaceJson,
                               QWidget *parent)
    : QDialog(parent), ui(new Ui::HtmlEditDialog)
{
    ui->setupUi(this);
    resize(1280, 760);

    QFont mono("Monospace", 9);
    mono.setStyleHint(QFont::Monospace);
    mono.setFixedPitch(true);
    ui->codeEdit->setFont(mono);
    ui->codeEdit->setTabStopDistance(28);

    m_simplePreview = ui->simplePreview->webView();
    m_wsPreview     = ui->wsPreview->webView();

    for (int i = 0; i < kLegacyPresetCount; ++i)
        ui->presetList->addItem(kLegacyPresets[i].name);
    ui->presetList->addItem("Custom HTML");

    setupPalette();

    connect(ui->presetList,  &QListWidget::currentRowChanged,
            this,            &HtmlEditDialog::onPresetSelected);
    connect(ui->browseBtn,   &QPushButton::clicked, this, &HtmlEditDialog::onBrowse);
    connect(ui->clearFileBtn,&QPushButton::clicked, this, &HtmlEditDialog::onClearFile);
    connect(ui->refreshBtn,  &QPushButton::clicked, this, &HtmlEditDialog::onRefreshSimplePreview);
    connect(ui->buttonBox,   &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(ui->buttonBox,   &QDialogButtonBox::accepted, this, [this]() {
        if (editMode() == EditMode::Workspace && currentWorkspace().isEmpty()) {
            ui->wsErrorLabel->setText(tr("Add at least one component to the canvas."));
            ui->modeTabs->setCurrentWidget(ui->workspaceTab);
            return;
        }
        accept();
    });
    connect(ui->modeTabs,    &QTabWidget::currentChanged, this, &HtmlEditDialog::onModeTabChanged);

    connect(ui->workspaceCanvas, &HtmlWorkspaceCanvasWidget::workspaceChanged,
            this,                &HtmlEditDialog::onWorkspaceChanged);
    connect(ui->workspaceCanvas, &HtmlWorkspaceCanvasWidget::componentSelected,
            this,                &HtmlEditDialog::onComponentSelected);

    connect(ui->propXSpin, &QDoubleSpinBox::valueChanged, this, &HtmlEditDialog::onPropSpinChanged);
    connect(ui->propYSpin, &QDoubleSpinBox::valueChanged, this, &HtmlEditDialog::onPropSpinChanged);
    connect(ui->propWSpin, &QDoubleSpinBox::valueChanged, this, &HtmlEditDialog::onPropSpinChanged);
    connect(ui->propHSpin, &QDoubleSpinBox::valueChanged, this, &HtmlEditDialog::onPropSpinChanged);
    connect(ui->deleteCompBtn,    &QPushButton::clicked, this, &HtmlEditDialog::onDeleteComponent);
    connect(ui->duplicateCompBtn, &QPushButton::clicked, this, &HtmlEditDialog::onDuplicateComponent);
    connect(ui->buildPreviewBtn,  &QPushButton::clicked, this, &HtmlEditDialog::onBuildWorkspacePreview);

    const HtmlWorkspace initialWs = HtmlWorkspace::fromJsonString(initialWorkspaceJson);
    if (!initialWs.isEmpty()) {
        ui->workspaceCanvas->setWorkspace(initialWs);
        ui->modeTabs->setCurrentWidget(ui->workspaceTab);
        onBuildWorkspacePreview();
    } else if (!initialHtml.isEmpty()) {
        ui->codeEdit->setPlainText(initialHtml);
        int matchRow = kLegacyPresetCount;
        for (int i = 0; i < kLegacyPresetCount; ++i) {
            if (loadResource(kLegacyPresets[i].resource) == initialHtml.trimmed()) {
                matchRow = i;
                break;
            }
        }
        ui->presetList->blockSignals(true);
        ui->presetList->setCurrentRow(matchRow);
        ui->presetList->blockSignals(false);
        loadSimplePreview(initialHtml);
    } else {
        ui->presetList->setCurrentRow(0);
    }

    syncPropsFromSelection();
}

HtmlEditDialog::~HtmlEditDialog() {
    delete ui;
}

void HtmlEditDialog::setupPalette() {
    const int idx = ui->paletteLayout->indexOf(ui->componentPalette);
    delete ui->componentPalette;

    auto *palette = new DraggablePaletteWidget;
    palette->setDragDropMode(QAbstractItemView::NoDragDrop);
    for (const HtmlPresetInfo &preset : HtmlPresetRegistry::presets()) {
        auto *item = new QListWidgetItem(preset.displayName);
        item->setData(Qt::UserRole, preset.id);
        palette->addItem(item);
    }
    ui->paletteLayout->insertWidget(idx, palette);
}

HtmlEditDialog::EditMode HtmlEditDialog::editMode() const {
    return ui->modeTabs->currentWidget() == ui->workspaceTab
               ? EditMode::Workspace
               : EditMode::Simple;
}

QString HtmlEditDialog::resultHtml() const {
    if (editMode() == EditMode::Workspace)
        return resultBakedHtml();
    if (!ui->filePathEdit->text().isEmpty())
        return {};
    return ui->codeEdit->toPlainText();
}

QString HtmlEditDialog::resultFilePath() const {
    if (editMode() == EditMode::Workspace)
        return {};
    return ui->filePathEdit->text().trimmed();
}

QString HtmlEditDialog::resultWorkspaceJson() const {
    if (editMode() != EditMode::Workspace)
        return {};
    return currentWorkspace().toJsonString();
}

QString HtmlEditDialog::resultBakedHtml() const {
    if (editMode() == EditMode::Workspace)
        return HtmlWorkspaceBuilder::build(currentWorkspace());
    const QString fp = ui->filePathEdit->text().trimmed();
    if (!fp.isEmpty()) {
        QFile f(fp);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            return QString::fromUtf8(f.readAll());
        return {};
    }
    return ui->codeEdit->toPlainText();
}

HtmlWorkspace HtmlEditDialog::currentWorkspace() const {
    return ui->workspaceCanvas->workspace();
}

void HtmlEditDialog::onPresetSelected(int row) {
    if (row < 0 || row >= kLegacyPresetCount)
        return;
    ui->filePathEdit->clear();
    const QString html = loadResource(kLegacyPresets[row].resource);
    if (!html.isEmpty()) {
        ui->codeEdit->setPlainText(html);
        loadSimplePreview(html);
    }
}

void HtmlEditDialog::onBrowse() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open HTML File"), {},
        tr("HTML Files (*.html *.htm);;All Files (*)"));
    if (path.isEmpty())
        return;

    ui->filePathEdit->setText(path);
    ui->presetList->blockSignals(true);
    ui->presetList->setCurrentRow(kLegacyPresetCount);
    ui->presetList->blockSignals(false);

    QFile f(path);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text))
        ui->codeEdit->setPlainText(QString::fromUtf8(f.readAll()));

    loadSimplePreview({}, path);
}

void HtmlEditDialog::onClearFile() {
    ui->filePathEdit->clear();
    ui->errorLabel->clear();
}

void HtmlEditDialog::onRefreshSimplePreview() {
    const QString fp = ui->filePathEdit->text().trimmed();
    if (!fp.isEmpty())
        loadSimplePreview({}, fp);
    else
        loadSimplePreview(ui->codeEdit->toPlainText());
}

void HtmlEditDialog::loadSimplePreview(const QString &html, const QString &filePath) {
    ui->errorLabel->clear();
    if (!filePath.isEmpty())
        m_simplePreview->load(QUrl::fromLocalFile(filePath));
    else
        m_simplePreview->setHtml(html, QUrl("qrc:/"));
}

void HtmlEditDialog::loadWorkspacePreview(const QString &html) {
    ui->wsErrorLabel->clear();
    m_wsPreview->setHtml(html, QUrl("qrc:/"));
}

void HtmlEditDialog::onWorkspaceChanged() {
    syncPropsFromSelection();
    if (editMode() == EditMode::Workspace && !currentWorkspace().isEmpty())
        loadWorkspacePreview(HtmlWorkspaceBuilder::build(currentWorkspace()));
}

void HtmlEditDialog::onComponentSelected(int index) {
    Q_UNUSED(index);
    syncPropsFromSelection();
}

void HtmlEditDialog::syncPropsFromSelection() {
    const HtmlWorkspace ws = currentWorkspace();
    const int idx = ui->workspaceCanvas->selectedIndex();
    const bool hasSel = idx >= 0 && idx < ws.components.size();

    ui->deleteCompBtn->setEnabled(hasSel);
    ui->duplicateCompBtn->setEnabled(hasSel);
    ui->propXSpin->setEnabled(hasSel);
    ui->propYSpin->setEnabled(hasSel);
    ui->propWSpin->setEnabled(hasSel);
    ui->propHSpin->setEnabled(hasSel);

    if (!hasSel) {
        ui->selectedNameLabel->setText(tr("No selection"));
        return;
    }

    const HtmlWorkspaceComponent &c = ws.components[idx];
    const HtmlPresetInfo *info = HtmlPresetRegistry::find(c.presetId);
    ui->selectedNameLabel->setText(info ? info->displayName : c.presetId);

    m_propChanging = true;
    ui->propXSpin->setValue(c.x * 100.0);
    ui->propYSpin->setValue(c.y * 100.0);
    ui->propWSpin->setValue(c.w * 100.0);
    ui->propHSpin->setValue(c.h * 100.0);
    m_propChanging = false;
}

void HtmlEditDialog::applyPropsToSelection() {
    if (m_propChanging)
        return;

    HtmlWorkspace ws = currentWorkspace();
    const int idx = ui->workspaceCanvas->selectedIndex();
    if (idx < 0 || idx >= ws.components.size())
        return;

    HtmlWorkspaceComponent &c = ws.components[idx];
    c.x = static_cast<float>(ui->propXSpin->value() / 100.0);
    c.y = static_cast<float>(ui->propYSpin->value() / 100.0);
    c.w = static_cast<float>(ui->propWSpin->value() / 100.0);
    c.h = static_cast<float>(ui->propHSpin->value() / 100.0);
    ui->workspaceCanvas->setWorkspace(ws);
    ui->workspaceCanvas->setSelectedIndex(idx);
}

void HtmlEditDialog::onPropSpinChanged() {
    applyPropsToSelection();
}

void HtmlEditDialog::onDeleteComponent() {
    HtmlWorkspace ws = currentWorkspace();
    const int idx = ui->workspaceCanvas->selectedIndex();
    if (idx < 0 || idx >= ws.components.size())
        return;
    ws.components.removeAt(idx);
    ui->workspaceCanvas->setWorkspace(ws);
    ui->workspaceCanvas->setSelectedIndex(-1);
    syncPropsFromSelection();
}

void HtmlEditDialog::onDuplicateComponent() {
    HtmlWorkspace ws = currentWorkspace();
    const int idx = ui->workspaceCanvas->selectedIndex();
    if (idx < 0 || idx >= ws.components.size())
        return;

    HtmlWorkspaceComponent copy = ws.components[idx];
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.x = std::min(copy.x + 0.03f, 1.f - copy.w);
    copy.y = std::min(copy.y + 0.03f, 1.f - copy.h);
    copy.zIndex = ws.components.size();
    ws.components.append(copy);
    ui->workspaceCanvas->setWorkspace(ws);
    ui->workspaceCanvas->setSelectedIndex(ws.components.size() - 1);
    syncPropsFromSelection();
}

void HtmlEditDialog::onBuildWorkspacePreview() {
    if (currentWorkspace().isEmpty()) {
        ui->wsErrorLabel->setText(tr("Add at least one component to the canvas."));
        return;
    }
    loadWorkspacePreview(HtmlWorkspaceBuilder::build(currentWorkspace()));
}

void HtmlEditDialog::onModeTabChanged(int index) {
    Q_UNUSED(index);
    if (editMode() == EditMode::Workspace)
        onBuildWorkspacePreview();
}
