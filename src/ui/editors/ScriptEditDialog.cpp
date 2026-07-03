#include "ui/editors/ScriptEditDialog.h"
#include "ui_ScriptEditDialog.h"

#include <QFile>
#include <QFont>
#include <QListWidgetItem>
#include <QMutexLocker>

namespace {

struct Preset { const char *name; const char *resource; const char *fallback; };

static const Preset kPresets[] = {
    { "Live Clock",    ":/scripts/clock.lua", R"(-- Live clock — wire ScriptOut → DataIn on a Text source with template: Time: {now}
return {
  now = os.date("%H:%M:%S"),
  date = os.date("%Y-%m-%d"),
})" },
    { "Counter",       ":/scripts/counter.lua", R"(-- Simple incrementing counter (resets when the script node is recreated)
if not _G.__prism_count then _G.__prism_count = 0 end
_G.__prism_count = _G.__prism_count + 1
return { count = _G.__prism_count })" },
    { "Greeting",      ":/scripts/greeting.lua", R"(-- Greeting with day-of-week — template: {greeting}, today is {weekday}
local t = os.date("*t")
local names = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" }
return {
  greeting = "Hello",
  weekday = names[t.wday] or "?",
  hour = t.hour,
})" },
    { "Bitcoin Price", ":/scripts/bitcoin_price.lua", R"(-- Fetch a public JSON API and expose a field to Text templates.
-- Example Text template: BTC: {price}
local body = http.get("https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd", 15000)
if body == "" or body == nil then
  return { price = "…", error = "request failed" }
end

local price = body:match('"usd"%s*:%s*([0-9%.]+)')
if not price then
  return { price = "…", error = "parse failed" }
end
return { price = "$" .. price })" },
};
static const int kPresetCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

static QString loadResource(const char *path, const char *fallback) {
    QFile f(path);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString fromQrc = QString::fromUtf8(f.readAll()).trimmed();
        if (!fromQrc.isEmpty())
            return fromQrc;
    }
    return QString::fromUtf8(fallback).trimmed();
}

} // namespace

ScriptEditDialog::ScriptEditDialog(const QString &initialCode,
                                   ScriptTriggerMode trigger,
                                   int intervalMs,
                                   QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ScriptEditDialog)
{
    ui->setupUi(this);

    QFont mono(QStringLiteral("Monospace"), 9);
    mono.setStyleHint(QFont::Monospace);
    mono.setFixedPitch(true);
    ui->codeEdit->setFont(mono);
    ui->codeEdit->setTabStopDistance(28);

    for (int i = 0; i < kPresetCount; ++i)
        ui->presetList->addItem(kPresets[i].name);
    ui->presetList->addItem(QStringLiteral("Custom Script"));

    m_previewOutput = std::make_shared<ScriptOutput>();
    m_previewRuntime = std::make_unique<ScriptRuntime>(m_previewOutput);
    connect(m_previewRuntime.get(), &ScriptRuntime::executionFinished, this,
            [this](bool ok) {
                if (ok) {
                    ui->errorLabel->clear();
                    QMutexLocker lock(&m_previewOutput->mutex);
                    ui->outputEdit->setPlainText(m_previewOutput->json);
                } else {
                    ui->errorLabel->setText(m_previewRuntime->lastError());
                }
            });

    ui->triggerCombo->setCurrentIndex(static_cast<int>(trigger));
    ui->intervalSpin->setValue(intervalMs);
    updateTriggerControls();

    connect(ui->presetList, &QListWidget::currentRowChanged,
            this, &ScriptEditDialog::onPresetSelected);
    connect(ui->presetList, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *item) {
                if (!item) return;
                applyPreset(ui->presetList->row(item));
            });
    connect(ui->runNowBtn, &QPushButton::clicked, this, &ScriptEditDialog::onRunNow);
    connect(ui->triggerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ScriptEditDialog::onTriggerChanged);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    if (!initialCode.isEmpty()) {
        ui->codeEdit->setPlainText(initialCode);
        int matchRow = kPresetCount;
        const QString trimmed = initialCode.trimmed();
        for (int i = 0; i < kPresetCount; ++i) {
            if (loadResource(kPresets[i].resource, kPresets[i].fallback) == trimmed) {
                matchRow = i;
                break;
            }
        }
        ui->presetList->blockSignals(true);
        ui->presetList->setCurrentRow(matchRow);
        ui->presetList->blockSignals(false);
    } else {
        ui->presetList->setCurrentRow(0);
        applyPreset(0);
    }
}

ScriptEditDialog::~ScriptEditDialog() {
    delete ui;
}

QString ScriptEditDialog::resultCode() const {
    return ui->codeEdit->toPlainText();
}

ScriptTriggerMode ScriptEditDialog::resultTriggerMode() const {
    return static_cast<ScriptTriggerMode>(ui->triggerCombo->currentIndex());
}

int ScriptEditDialog::resultIntervalMs() const {
    return ui->intervalSpin->value();
}

void ScriptEditDialog::onPresetSelected(int row) {
    applyPreset(row);
}

void ScriptEditDialog::applyPreset(int row) {
    if (row < 0 || row >= kPresetCount) return;
    const QString code = loadResource(kPresets[row].resource, kPresets[row].fallback);
    if (!code.isEmpty())
        ui->codeEdit->setPlainText(code);
}

void ScriptEditDialog::onRunNow() {
    runPreview();
}

void ScriptEditDialog::onTriggerChanged(int) {
    updateTriggerControls();
}

void ScriptEditDialog::updateTriggerControls() {
    const bool periodic = ui->triggerCombo->currentIndex()
        == static_cast<int>(ScriptTriggerMode::Periodic);
    ui->intervalLabel->setEnabled(periodic);
    ui->intervalSpin->setEnabled(periodic);
}

void ScriptEditDialog::runPreview() {
    if (!m_previewRuntime) return;
    m_previewRuntime->setScript(ui->codeEdit->toPlainText());
    m_previewRuntime->runNow();
}
