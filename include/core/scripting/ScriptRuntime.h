#pragma once

#include "core/scripting/ScriptOutput.h"
#include <QObject>
#include <QString>
#include <atomic>
#include <memory>

enum class ScriptTriggerMode {
    Periodic = 0,
    OnStart  = 1,
    Manual   = 2,
};

class QTimer;
class QNetworkAccessManager;
class QNetworkReply;

/// Runs a user Lua script (via sol2) on a trigger — periodically, once at start,
/// or manually — and publishes the values it sets into a shared ScriptOutput
/// that TextSource templates read from.
class ScriptRuntime : public QObject {
    Q_OBJECT

public:
    explicit ScriptRuntime(std::shared_ptr<ScriptOutput> output,
                           QObject *parent = nullptr);
    ~ScriptRuntime() override;

    void setScript(const QString &code);
    void setTrigger(ScriptTriggerMode mode, int intervalMs = 1000);
    ScriptTriggerMode triggerMode() const { return m_triggerMode; }
    int intervalMs() const { return m_intervalMs; }
    QString lastError() const { return m_lastError; }
    QString lastLog() const { return m_lastLog; }

public slots:
    void applySettings(const QString &code, int triggerMode, int intervalMs);
    void runNow();
    void shutdown();

signals:
    void executionFinished(bool ok);

private slots:
    void onPeriodicTimeout();

private:
    void executeScript();
    void writeOutput(const QString &json, const QString &log);
    void setupEngine();
    void teardownEngine();

    std::shared_ptr<ScriptOutput> m_output;
    QString m_script;
    ScriptTriggerMode m_triggerMode = ScriptTriggerMode::Periodic;
    int m_intervalMs = 1000;
    QString m_lastError;
    QString m_lastLog;
    QTimer *m_timer = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_activeReply = nullptr;
    std::atomic<bool> m_shuttingDown{false};

#ifdef SWITCHX_HAVE_LUA
    struct LuaState;
    std::unique_ptr<LuaState> m_lua;
#endif
};
