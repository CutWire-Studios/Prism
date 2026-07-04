#include "ui/editors/HtmlEditDialog.h"
#include "ui_HtmlEditDialog.h"
#include "core/sources/HtmlWorkspace.h"
#include <algorithm>
#include <QWebEngineView>
#include <QColorDialog>
#include <QFileDialog>
#include <QFile>
#include <QFont>
#include <QUrl>
#include <QFileInfo>
#include <QListWidgetItem>
#include <QUuid>

namespace {

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

// Component indices ordered top layer first (highest zIndex first).
static QList<int> zOrderTopFirst(const HtmlWorkspace &ws) {
    QList<int> order;
    for (int i = 0; i < ws.components.size(); ++i)
        order.append(i);
    std::sort(order.begin(), order.end(), [&ws](int a, int b) {
        return ws.components[a].zIndex > ws.components[b].zIndex;
    });
    return order;
}

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

    for (int i = 0; i < kLegacyPresetCount; ++i)
        ui->presetList->addItem(kLegacyPresets[i].name);
    ui->presetList->addItem("Custom HTML");

    connect(ui->presetList,  &QListWidget::currentRowChanged,
            this,            &HtmlEditDialog::onPresetSelected);
    connect(ui->browseBtn,   &QPushButton::clicked, this, &HtmlEditDialog::onBrowse);
    connect(ui->clearFileBtn,&QPushButton::clicked, this, &HtmlEditDialog::onClearFile);
    connect(ui->refreshBtn,  &QPushButton::clicked, this, &HtmlEditDialog::onRefreshSimplePreview);
    connect(ui->buttonBox,   &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(ui->buttonBox,   &QDialogButtonBox::accepted, this, [this]() {
        syncVisualToCode();
        if (editMode() == EditMode::Workspace && currentWorkspace().isEmpty()) {
            ui->wsErrorLabel->setText(tr("Add at least one component to the canvas."));
            ui->modeTabs->setCurrentWidget(ui->workspaceTab);
            return;
        }
        accept();
    });

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

    connect(ui->snapBtn,     &QToolButton::toggled,
            ui->workspaceCanvas, &HtmlWorkspaceCanvasWidget::setSnapEnabled);
    connect(ui->safeAreaBtn, &QToolButton::toggled,
            ui->workspaceCanvas, &HtmlWorkspaceCanvasWidget::setSafeAreasVisible);

    connect(ui->layerList, &QListWidget::currentRowChanged,
            this,          &HtmlEditDialog::onLayerRowChanged);
    connect(ui->layerList, &QListWidget::itemChanged,
            this,          &HtmlEditDialog::onLayerItemChanged);
    connect(ui->layerUpBtn,   &QToolButton::clicked, this, &HtmlEditDialog::onLayerMoveUp);
    connect(ui->layerDownBtn, &QToolButton::clicked, this, &HtmlEditDialog::onLayerMoveDown);

    connect(ui->visualViewBtn, &QToolButton::toggled,
            this,              &HtmlEditDialog::onSimpleViewChanged);
    connect(ui->visualEditor, &HtmlVisualEditorWidget::htmlEdited,
            this,             &HtmlEditDialog::onVisualHtmlEdited);
    connect(ui->visualEditor, &HtmlVisualEditorWidget::selectionChanged,
            this,             &HtmlEditDialog::onVisualSelection);
    connect(ui->inspFontSizeSpin, &QSpinBox::valueChanged,
            this,                 &HtmlEditDialog::onInspFontSizeChanged);
    connect(ui->inspOpacitySpin,  &QSpinBox::valueChanged,
            this,                 &HtmlEditDialog::onInspOpacityChanged);
    connect(ui->inspTextColorBtn, &QPushButton::clicked, this, &HtmlEditDialog::onInspTextColor);
    connect(ui->inspBgColorBtn,   &QPushButton::clicked, this, &HtmlEditDialog::onInspBgColor);
    connect(ui->inspDeleteBtn,    &QPushButton::clicked,
            ui->visualEditor,     &HtmlVisualEditorWidget::deleteSelectedElement);
    onVisualSelection(QJsonObject());

    const HtmlWorkspace initialWs = HtmlWorkspace::fromJsonString(initialWorkspaceJson);
    if (!initialWs.isEmpty()) {
        ui->workspaceCanvas->setWorkspace(initialWs);
        ui->modeTabs->setCurrentWidget(ui->workspaceTab);
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
        ui->visualViewBtn->setChecked(true); // editing existing HTML: start visual
    } else {
        ui->presetList->setCurrentRow(0);
    }

    ui->inspFontSizeSpin->setKeyboardTracking(false);
    ui->inspOpacitySpin->setKeyboardTracking(false);

    syncPropsFromSelection();
    rebuildLayerList();
}

HtmlEditDialog::~HtmlEditDialog() {
    delete ui;
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
    if (ui->visualViewBtn->isChecked())
        enterVisualView();
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

void HtmlEditDialog::onWorkspaceChanged() {
    ui->wsErrorLabel->clear();
    syncPropsFromSelection();
    rebuildLayerList();
}

void HtmlEditDialog::onComponentSelected(int index) {
    Q_UNUSED(index);
    syncPropsFromSelection();
    rebuildLayerList();
}

void HtmlEditDialog::syncPropsFromSelection() {
    const HtmlWorkspace ws = currentWorkspace();
    const int idx = ui->workspaceCanvas->selectedIndex();
    const bool hasSel = idx >= 0 && idx < ws.components.size();

    ui->deleteCompBtn->setEnabled(hasSel);
    ui->duplicateCompBtn->setEnabled(hasSel);
    ui->layerUpBtn->setEnabled(hasSel);
    ui->layerDownBtn->setEnabled(hasSel);
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
    rebuildLayerList();
}

void HtmlEditDialog::onDuplicateComponent() {
    HtmlWorkspace ws = currentWorkspace();
    const int idx = ui->workspaceCanvas->selectedIndex();
    if (idx < 0 || idx >= ws.components.size())
        return;

    int maxZ = -1;
    for (const auto &c : ws.components)
        maxZ = std::max(maxZ, c.zIndex);

    HtmlWorkspaceComponent copy = ws.components[idx];
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.x = std::min(copy.x + 0.03f, 1.f - copy.w);
    copy.y = std::min(copy.y + 0.03f, 1.f - copy.h);
    copy.zIndex = maxZ + 1;
    ws.components.append(copy);
    ui->workspaceCanvas->setWorkspace(ws);
    ui->workspaceCanvas->setSelectedIndex(ws.components.size() - 1);
    syncPropsFromSelection();
    rebuildLayerList();
}

void HtmlEditDialog::rebuildLayerList() {
    const HtmlWorkspace ws = currentWorkspace();
    const QList<int> order = zOrderTopFirst(ws);
    const int selIdx = ui->workspaceCanvas->selectedIndex();

    QString signature;
    for (int i : order) {
        const auto &c = ws.components[i];
        signature += QStringLiteral("%1:%2;").arg(c.id).arg(c.visible ? 1 : 0);
    }
    signature += QStringLiteral("|%1").arg(selIdx);
    if (signature == m_layerSignature)
        return;
    m_layerSignature = signature;

    m_layerSyncing = true;
    ui->layerList->clear();
    m_layerRowToComp = order;
    for (int row = 0; row < order.size(); ++row) {
        const HtmlWorkspaceComponent &c = ws.components[order[row]];
        const HtmlPresetInfo *info = HtmlPresetRegistry::find(c.presetId);
        auto *item = new QListWidgetItem(info ? info->displayName : c.presetId);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(c.visible ? Qt::Checked : Qt::Unchecked);
        ui->layerList->addItem(item);
        if (order[row] == selIdx)
            ui->layerList->setCurrentRow(row);
    }
    m_layerSyncing = false;
}

void HtmlEditDialog::onLayerRowChanged(int row) {
    if (m_layerSyncing || row < 0 || row >= m_layerRowToComp.size())
        return;
    ui->workspaceCanvas->setSelectedIndex(m_layerRowToComp[row]);
    syncPropsFromSelection();
}

void HtmlEditDialog::onLayerItemChanged(QListWidgetItem *item) {
    if (m_layerSyncing)
        return;
    const int row = ui->layerList->row(item);
    if (row < 0 || row >= m_layerRowToComp.size())
        return;

    HtmlWorkspace ws = currentWorkspace();
    ws.components[m_layerRowToComp[row]].visible = item->checkState() == Qt::Checked;
    ui->workspaceCanvas->setWorkspace(ws);
}

void HtmlEditDialog::moveSelectedLayer(int delta) {
    HtmlWorkspace ws = currentWorkspace();
    const int selIdx = ui->workspaceCanvas->selectedIndex();
    if (selIdx < 0 || selIdx >= ws.components.size())
        return;

    QList<int> order = zOrderTopFirst(ws);
    const int row = order.indexOf(selIdx);
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= order.size())
        return;

    order.swapItemsAt(row, target);
    for (int r = 0; r < order.size(); ++r)
        ws.components[order[r]].zIndex = order.size() - 1 - r;

    ui->workspaceCanvas->setWorkspace(ws);
    ui->workspaceCanvas->setSelectedIndex(selIdx);
    m_layerSignature.clear();
    rebuildLayerList();
}

void HtmlEditDialog::onLayerMoveUp() {
    moveSelectedLayer(-1);
}

void HtmlEditDialog::onLayerMoveDown() {
    moveSelectedLayer(1);
}

void HtmlEditDialog::enterVisualView() {
    const QString fp = ui->filePathEdit->text().trimmed();
    QString html;
    QUrl base;
    if (!fp.isEmpty()) {
        QFile f(fp);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            html = QString::fromUtf8(f.readAll());
        base = QUrl::fromLocalFile(fp);
    } else {
        html = ui->codeEdit->toPlainText();
    }
    ui->visualEditor->loadHtml(html, base);
}

void HtmlEditDialog::syncVisualToCode() {
    if (!ui->visualEditor->isDirty())
        return;
    ui->codeEdit->setPlainText(ui->visualEditor->html());
    ui->filePathEdit->clear();
}

void HtmlEditDialog::onSimpleViewChanged(bool visual) {
    if (visual) {
        enterVisualView();
        ui->simpleStack->setCurrentWidget(ui->visualPage);
    } else {
        syncVisualToCode();
        ui->simpleStack->setCurrentWidget(ui->codePage);
        onRefreshSimplePreview();
    }
}

void HtmlEditDialog::onVisualHtmlEdited() {
    // Content diverged from the file on disk — the source is inline HTML now.
    if (!ui->filePathEdit->text().isEmpty())
        ui->filePathEdit->clear();
}

void HtmlEditDialog::setColorSwatch(QPushButton *btn, const QColor &color) {
    if (color.isValid()) {
        const QString fg = color.lightness() > 128 ? "#000000" : "#ffffff";
        btn->setStyleSheet(QStringLiteral("background-color:%1;color:%2;")
                               .arg(color.name(), fg));
        btn->setText(color.name().toUpper());
    } else {
        btn->setStyleSheet({});
        btn->setText(tr("None"));
    }
}

void HtmlEditDialog::onVisualSelection(const QJsonObject &info) {
    const bool hasSel = !info.isEmpty();

    ui->inspElementLabel->setText(hasSel ? info["tag"].toString()
                                         : tr("Nothing selected"));
    ui->inspFontSizeSpin->setEnabled(hasSel);
    ui->inspOpacitySpin->setEnabled(hasSel);
    ui->inspTextColorBtn->setEnabled(hasSel);
    ui->inspBgColorBtn->setEnabled(hasSel);
    ui->inspDeleteBtn->setEnabled(hasSel);

    m_inspChanging = true;
    if (hasSel) {
        ui->inspFontSizeSpin->setValue(info["fontSize"].toInt());
        ui->inspOpacitySpin->setValue(info["opacity"].toInt());
        m_inspTextColor = QColor(info["color"].toString());
        m_inspBgColor   = QColor(info["background"].toString());
    } else {
        m_inspTextColor = QColor();
        m_inspBgColor   = QColor();
    }
    setColorSwatch(ui->inspTextColorBtn, m_inspTextColor);
    setColorSwatch(ui->inspBgColorBtn, m_inspBgColor);
    m_inspChanging = false;
}

void HtmlEditDialog::onInspFontSizeChanged(int px) {
    if (m_inspChanging)
        return;
    ui->visualEditor->applyStyle(QStringLiteral("font-size"),
                                 QStringLiteral("%1px").arg(px));
}

void HtmlEditDialog::onInspOpacityChanged(int percent) {
    if (m_inspChanging)
        return;
    ui->visualEditor->applyStyle(QStringLiteral("opacity"),
                                 QString::number(percent / 100.0));
}

void HtmlEditDialog::onInspTextColor() {
    const QColor c = QColorDialog::getColor(
        m_inspTextColor.isValid() ? m_inspTextColor : QColor(Qt::white),
        this, tr("Text Color"));
    if (!c.isValid())
        return;
    m_inspTextColor = c;
    setColorSwatch(ui->inspTextColorBtn, c);
    ui->visualEditor->applyStyle(QStringLiteral("color"), c.name());
}

void HtmlEditDialog::onInspBgColor() {
    const QColor c = QColorDialog::getColor(
        m_inspBgColor.isValid() ? m_inspBgColor : QColor(Qt::black),
        this, tr("Fill Color"), QColorDialog::ShowAlphaChannel);
    if (!c.isValid())
        return;
    m_inspBgColor = c;
    setColorSwatch(ui->inspBgColorBtn, c);
    ui->visualEditor->applyStyle(
        QStringLiteral("background-color"),
        c.alpha() == 255
            ? c.name()
            : QStringLiteral("rgba(%1,%2,%3,%4)")
                  .arg(c.red()).arg(c.green()).arg(c.blue())
                  .arg(c.alphaF(), 0, 'f', 3));
}
