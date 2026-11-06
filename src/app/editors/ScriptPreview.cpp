#include "app/editors/ScriptPreview.h"

#include "core/scripting/ScriptRuntime.h"

#include <QFile>
#include <QMutexLocker>

namespace {

struct Preset {
    const char *name;
    const char *resource;
};

const Preset kPresets[] = {
    { "Live Clock",     ":/scripts/clock.lua" },
    { "Counter",        ":/scripts/counter.lua" },
    { "Greeting",       ":/scripts/greeting.lua" },
    { "Bitcoin Price",  ":/scripts/bitcoin_price.lua" },
};
constexpr int kPresetCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

} // namespace

ScriptPreview::ScriptPreview(QObject *parent)
    : QObject(parent)
    , m_scriptOutput(std::make_shared<ScriptOutput>())
    , m_runtime(std::make_unique<ScriptRuntime>(m_scriptOutput))
{
    connect(m_runtime.get(), &ScriptRuntime::executionFinished, this, [this](bool ok) {
        if (ok) {
            m_error.clear();
            QMutexLocker lock(&m_scriptOutput->mutex);
            m_output = m_scriptOutput->json;
        } else {
            m_error = m_runtime->lastError();
        }
        emit resultChanged();
    });
}

ScriptPreview::~ScriptPreview() = default;

QStringList ScriptPreview::presetNames() const
{
    QStringList names;
    for (int i = 0; i < kPresetCount; ++i)
        names.append(QString::fromUtf8(kPresets[i].name));
    return names;
}

QString ScriptPreview::presetCode(int row) const
{
    if (row < 0 || row >= kPresetCount)
        return {};
    QFile file(QString::fromUtf8(kPresets[row].resource));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll()).trimmed();
}

int ScriptPreview::presetIndexFor(const QString &code) const
{
    const QString trimmed = code.trimmed();
    for (int i = 0; i < kPresetCount; ++i) {
        if (presetCode(i) == trimmed)
            return i;
    }
    return -1;
}

void ScriptPreview::run(const QString &code)
{
    m_runtime->setScript(code);
    m_runtime->runNow();
}

void ScriptPreview::clear()
{
    m_output.clear();
    m_error.clear();
    emit resultChanged();
}
