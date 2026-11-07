#pragma once

#include "core/scripting/ScriptOutput.h"
#include <QDate>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTime>
#include <QVariant>
#include <QVariantList>
#include <atomic>
#include <functional>
#include <memory>

enum class ScriptTriggerMode {
    OnInputChange = 0,   // re-run whenever an upstream node's output changes
    Interval      = 1,
    TimeOfDay     = 2,   // once a day, at the first poll at or after the set time
    Manual        = 3,
};

/// What a script may reach beyond its own sandbox. The callbacks run on the script's thread and
/// must be thread-safe; the owner is responsible for restricting them to one scene's graph.
struct ScriptHost {
    /// False when the node is not reachable. A missing parameter yields an invalid QVariant.
    std::function<bool(quint64 node, const QString &name, QVariant *out)> getParam;
    /// Queues the write for the GUI thread. False when the node is not reachable.
    std::function<bool(quint64 node, const QString &name, const QVariant &value)> setParam;
    /// low/mid/high/level/beat/hasAudio of one analyzer node, or of the most recent one for id 0.
    std::function<QVariantMap(quint64 node)> audioBands;
};

class QTimer;
class QNetworkAccessManager;
class QNetworkReply;

/// Runs a user Lua script (via sol2) in a sandbox and publishes the values it returns into a
/// shared ScriptOutput that TextSource templates read from. Also evaluates parameter expressions.
class ScriptRuntime : public QObject {
    Q_OBJECT

public:
    static constexpr int kLogLines = 200;
    static constexpr int kScriptInstructionLimit = 20'000'000;
    static constexpr int kExpressionInstructionLimit = 200'000;

    explicit ScriptRuntime(std::shared_ptr<ScriptOutput> output,
                           std::shared_ptr<ScriptHost> host = {},
                           QObject *parent = nullptr);
    ~ScriptRuntime() override;

    void setScript(const QString &code);
    void setInstructionLimit(int limit) { m_instructionLimit = limit; }
    QString lastError() const { return m_lastError; }
    QStringList log() const { return m_log; }

    /// Thread-safe. Aborts a running script at its next VM hook and ignores later requests; the
    /// owner then deletes the runtime from its thread.
    void requestShutdown();

public slots:
    void applySettings(const QString &code, int runOn, int intervalMs, const QString &runTime);
    /// An inactive runtime stops its timers and ignores input changes; runNow still works.
    void setActive(bool active);
    void runNow();
    void shutdown();
    /// Replace the JSON handed to the script as the global `input` table on its
    /// next run. In OnInputChange mode this also runs the script.
    void setInput(const QString &json);
    /// jobs: list of {node, name, expr}. Emits expressionsEvaluated with {node, name, ok, value, error}.
    void evaluateExpressions(const QVariantList &jobs, double t);

signals:
    void executionFinished(bool ok, const QString &error);
    void logChanged(const QStringList &lines);
    void expressionsEvaluated(const QVariantList &results);

private slots:
    void onTimeout();

private:
    void executeScript();
    void writeOutput(const QString &json);
    void publishPulse();
    void appendLog(const QString &line);
    void restartTimers();
    void setupEngine();
    void teardownEngine();

    std::shared_ptr<ScriptOutput> m_output;
    std::shared_ptr<ScriptHost> m_host;
    QString m_script;
    QString m_inputJson;   // latest upstream JSON, exposed as the `input` global
    ScriptTriggerMode m_runOn = ScriptTriggerMode::OnInputChange;
    int m_intervalMs = 1000;
    QTime m_runTime = QTime(12, 0);
    QDate m_lastFireDate;
    qint64 m_tickCount = 0;
    bool m_active = true;
    int m_instructionLimit = kScriptInstructionLimit;
    QString m_lastError;
    QStringList m_log;
    bool m_logDirty = false;
    QTimer *m_timer = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_activeReply = nullptr;
    std::atomic<bool> m_shuttingDown{false};

#ifdef PRISM_HAVE_LUA
    struct LuaState;
    std::unique_ptr<LuaState> m_lua;
#endif
};
