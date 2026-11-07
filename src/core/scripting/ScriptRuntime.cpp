#include "core/scripting/ScriptRuntime.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

#ifdef PRISM_HAVE_LUA
extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}
#include <sol/sol.hpp>
#endif

namespace {

constexpr int kMaxLogLineChars = 2000;

#ifdef PRISM_HAVE_LUA
QJsonValue luaToJsonValue(const sol::object &obj) {
    if (!obj.valid() || obj.get_type() == sol::type::lua_nil)
        return QJsonValue(QJsonValue::Null);

    switch (obj.get_type()) {
    case sol::type::boolean:
        return QJsonValue(obj.as<bool>());
    case sol::type::number: {
        const lua_Number n = obj.as<lua_Number>();
        const lua_Integer i = static_cast<lua_Integer>(n);
        if (static_cast<lua_Number>(i) == n)
            return QJsonValue(static_cast<qint64>(i));
        return QJsonValue(static_cast<double>(n));
    }
    case sol::type::string:
        return QJsonValue(QString::fromUtf8(obj.as<std::string>().c_str()));
    case sol::type::table: {
        const sol::table tbl = obj.as<sol::table>();
        bool isArray = true;
        int maxIndex = 0;
        for (const auto &pair : tbl) {
            if (pair.first.get_type() != sol::type::number) {
                isArray = false;
                break;
            }
            maxIndex = std::max(maxIndex, static_cast<int>(pair.first.as<lua_Integer>()));
        }
        if (isArray && maxIndex > 0) {
            QJsonArray arr;
            for (int i = 1; i <= maxIndex; ++i) {
                sol::object val = tbl.get<sol::object>(i);
                if (val.valid())
                    arr.append(luaToJsonValue(val));
                else
                    arr.append(QJsonValue(QJsonValue::Null));
            }
            return arr;
        }

        QJsonObject o;
        for (const auto &pair : tbl) {
            const QString key = QString::fromUtf8(pair.first.as<std::string>().c_str());
            o.insert(key, luaToJsonValue(pair.second));
        }
        return o;
    }
    default:
        break;
    }
    return QJsonValue(QString::fromUtf8(obj.as<std::string>().c_str()));
}

// Build a Lua value mirroring a JSON value, for the `input` global.
sol::object jsonToLua(sol::state_view lua, const QJsonValue &val) {
    switch (val.type()) {
    case QJsonValue::Bool:
        return sol::make_object(lua, val.toBool());
    case QJsonValue::Double: {
        const double d = val.toDouble();
        const qint64 i = static_cast<qint64>(d);
        if (static_cast<double>(i) == d)
            return sol::make_object(lua, static_cast<lua_Integer>(i));
        return sol::make_object(lua, d);
    }
    case QJsonValue::String:
        return sol::make_object(lua, val.toString().toUtf8().constData());
    case QJsonValue::Array: {
        sol::table t = lua.create_table();
        const QJsonArray arr = val.toArray();
        for (int i = 0; i < arr.size(); ++i)
            t[i + 1] = jsonToLua(lua, arr.at(i));   // 1-based, Lua convention
        return t;
    }
    case QJsonValue::Object: {
        sol::table t = lua.create_table();
        const QJsonObject obj = val.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it)
            t[it.key().toUtf8().constData()] = jsonToLua(lua, it.value());
        return t;
    }
    default:
        return sol::make_object(lua, sol::nil);
    }
}
#endif


#ifdef PRISM_HAVE_LUA
constexpr int kHookStep = 1000;

struct HookContext {
    const std::atomic<bool> *shuttingDown = nullptr;
    qint64 executed = 0;
    qint64 limit = 0;
};

void instructionHook(lua_State *L, lua_Debug *) {
    auto *ctx = *static_cast<HookContext **>(lua_getextraspace(L));
    ctx->executed += kHookStep;
    if (ctx->shuttingDown->load(std::memory_order_acquire))
        luaL_error(L, "script aborted");
    if (ctx->executed > ctx->limit)
        luaL_error(L, "instruction limit exceeded (%d instructions)", int(ctx->limit));
}

sol::object variantToLua(sol::state_view lua, const QVariant &v) {
    switch (v.typeId()) {
    case QMetaType::Bool:
        return sol::make_object(lua, v.toBool());
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return sol::make_object(lua, static_cast<lua_Integer>(v.toLongLong()));
    case QMetaType::Double:
    case QMetaType::Float:
        return sol::make_object(lua, v.toDouble());
    case QMetaType::QString:
        return sol::make_object(lua, v.toString().toUtf8().constData());
    case QMetaType::QVariantMap: {
        sol::table t = lua.create_table();
        const QVariantMap m = v.toMap();
        for (auto it = m.begin(); it != m.end(); ++it)
            t[it.key().toUtf8().constData()] = variantToLua(lua, it.value());
        return t;
    }
    default:
        return sol::make_object(lua, sol::nil);
    }
}

QVariant luaToVariant(const sol::object &o) {
    switch (o.get_type()) {
    case sol::type::boolean:
        return o.as<bool>();
    case sol::type::number:
        return o.as<double>();
    case sol::type::string:
        return QString::fromUtf8(o.as<std::string>().c_str());
    default:
        throw std::runtime_error("prism.param values must be numbers, booleans or strings");
    }
}

sol::table bandsToLua(sol::state_view lua, const QVariantMap &b) {
    sol::table t = lua.create_table();
    for (const char *k : {"low", "mid", "high", "level", "beat"})
        t[k] = 0.0;
    t["hasAudio"] = false;
    for (auto it = b.begin(); it != b.end(); ++it)
        t[it.key().toUtf8().constData()] = variantToLua(lua, it.value());
    return t;
}
#endif

} // namespace

ScriptRuntime::ScriptRuntime(std::shared_ptr<ScriptOutput> output, std::shared_ptr<ScriptHost> host,
                             QObject *parent)
    : QObject(parent)
    , m_output(std::move(output))
    , m_host(std::move(host))
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, &ScriptRuntime::onTimeout);

    m_network = new QNetworkAccessManager(this);

#ifdef PRISM_HAVE_LUA
    setupEngine();
#endif
}

ScriptRuntime::~ScriptRuntime() {
    shutdown();
#ifdef PRISM_HAVE_LUA
    teardownEngine();
#endif
}

void ScriptRuntime::setScript(const QString &code) {
    m_script = code;
}

void ScriptRuntime::requestShutdown() {
    m_shuttingDown.store(true, std::memory_order_release);
    QMetaObject::invokeMethod(this, "shutdown", Qt::QueuedConnection);
}

void ScriptRuntime::restartTimers() {
    m_timer->stop();
    m_lastFireDate = QDate();
    if (!m_active || m_shuttingDown.load(std::memory_order_acquire))
        return;
    if (m_runOn == ScriptTriggerMode::Interval) {
        m_timer->start(m_intervalMs);
    } else if (m_runOn == ScriptTriggerMode::TimeOfDay) {
        m_timer->start(1000);
        onTimeout();
    }
}

void ScriptRuntime::applySettings(const QString &code, int runOn, int intervalMs, const QString &runTime) {
    if (m_shuttingDown.load(std::memory_order_acquire))
        return;
    m_script = code;
    m_runOn = static_cast<ScriptTriggerMode>(std::clamp(runOn, 0, 3));
    m_intervalMs = std::max(20, intervalMs);
    m_runTime = QTime::fromString(runTime, QStringLiteral("HH:mm:ss"));
    if (!m_runTime.isValid())
        m_runTime = QTime(12, 0);
    restartTimers();
}

void ScriptRuntime::setActive(bool active) {
    if (m_active == active)
        return;
    m_active = active;
    restartTimers();
}

void ScriptRuntime::setInput(const QString &json) {
    if (m_shuttingDown.load(std::memory_order_acquire))
        return;
    m_inputJson = json;
    if (m_active && m_runOn == ScriptTriggerMode::OnInputChange)
        executeScript();
}

void ScriptRuntime::runNow() {
    if (m_shuttingDown.load(std::memory_order_acquire))
        return;
    executeScript();
}

void ScriptRuntime::shutdown() {
    m_shuttingDown.store(true, std::memory_order_release);
    if (m_timer)
        m_timer->stop();
    if (m_activeReply)
        m_activeReply->abort();
}

void ScriptRuntime::onTimeout() {
    if (m_shuttingDown.load(std::memory_order_acquire) || !m_active)
        return;
    if (m_runOn == ScriptTriggerMode::TimeOfDay) {
        const QDate today = QDate::currentDate();
        if (m_lastFireDate == today || QTime::currentTime() < m_runTime)
            return;
        m_lastFireDate = today;
    }
    executeScript();
}

void ScriptRuntime::writeOutput(const QString &json) {
    if (m_shuttingDown.load(std::memory_order_acquire) || !m_output)
        return;

    {
        QMutexLocker lock(&m_output->mutex);
        m_output->json = json;
    }
    m_output->version.fetch_add(1, std::memory_order_release);
}

void ScriptRuntime::publishPulse() {
    ++m_tickCount;
    const QJsonObject obj{{QStringLiteral("tick"), double(m_tickCount)},
                          {QStringLiteral("epoch"), double(QDateTime::currentSecsSinceEpoch())},
                          {QStringLiteral("pulse"), true}};
    writeOutput(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

void ScriptRuntime::appendLog(const QString &line) {
    m_log.append(line.left(kMaxLogLineChars));
    while (m_log.size() > kLogLines)
        m_log.removeFirst();
    m_logDirty = true;
}

#ifdef PRISM_HAVE_LUA
struct ScriptRuntime::LuaState {
    sol::state lua;
    HookContext hook;
    sol::environment exprEnv;
    std::unordered_map<std::string, sol::protected_function> exprCache;
};

void ScriptRuntime::setupEngine() {
    m_lua = std::make_unique<LuaState>();
    sol::state &lua = m_lua->lua;
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table,
                       sol::lib::os);

    sol::table prism = lua.create_named_table("prism");
    // The only parts of `os` that bundled scripts need; the library itself is removed below.
    sol::table os = lua["os"];
    prism["time"] = os.get<sol::function>("time");
    prism["clock"] = os.get<sol::function>("clock");
    prism["date"] = os.get<sol::function>("date");
    lua["os"] = sol::nil;
    lua["dofile"] = sol::nil;
    lua["loadfile"] = sol::nil;
    lua["require"] = sol::nil;
    lua["package"] = sol::nil;
    lua["io"] = sol::nil;
    lua["debug"] = sol::nil;
    lua.script("local l = load; load = function(c, n, m, e) return l(c, n, 't', e) end");

    sol::function tostr = lua["tostring"];
    auto printFn = [this, tostr](sol::variadic_args va) {
        QStringList parts;
        for (auto v : va)
            parts << QString::fromStdString(tostr(v).get<std::string>());
        appendLog(parts.join(QLatin1Char('\t')));
    };
    lua["print"] = printFn;
    prism["log"] = printFn;

    prism["param"] = [this](lua_Integer id, const std::string &name, sol::optional<sol::object> value,
                            sol::this_state ts) -> sol::object {
        if (!m_host || !m_host->getParam)
            throw std::runtime_error("prism.param is not available here");
        const QString qname = QString::fromUtf8(name.c_str());
        if (value) {
            if (!m_host->setParam(quint64(id), qname, luaToVariant(*value)))
                throw std::runtime_error("node " + std::to_string(id) + " is not in this cue");
            return sol::make_object(ts, sol::nil);
        }
        QVariant out;
        if (!m_host->getParam(quint64(id), qname, &out))
            throw std::runtime_error("node " + std::to_string(id) + " is not in this cue");
        return variantToLua(sol::state_view(ts.L), out);
    };

    sol::table audio = lua.create_table();
    prism["audio"] = audio;
    audio["bands"] = [this](sol::optional<lua_Integer> id, sol::this_state ts) -> sol::table {
        const QVariantMap b = (m_host && m_host->audioBands) ? m_host->audioBands(quint64(id.value_or(0)))
                                                              : QVariantMap{};
        return bandsToLua(sol::state_view(ts.L), b);
    };

    sol::table http = lua.create_named_table("http");
    http["get"] = [this](const std::string &urlStr, sol::optional<int> timeoutMs) -> std::string {
        if (m_shuttingDown.load(std::memory_order_acquire))
            return {};

        const QUrl url(QString::fromUtf8(urlStr.c_str()));
        if (!url.isValid())
            return {};

        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Prism/1.0"));
        QNetworkReply *reply = m_network->get(req);
        m_activeReply = reply;

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        const int ms = timeoutMs.value_or(10000);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        timer.start(ms);
        loop.exec();

        m_activeReply = nullptr;

        if (m_shuttingDown.load(std::memory_order_acquire)) {
            reply->deleteLater();
            return {};
        }

        if (!reply->isFinished()) {
            reply->abort();
            reply->deleteLater();
            return {};
        }

        const QByteArray body = reply->readAll();
        reply->deleteLater();
        return std::string(body.constData(), static_cast<size_t>(body.size()));
    };

    m_lua->hook.shuttingDown = &m_shuttingDown;
    *static_cast<HookContext **>(lua_getextraspace(lua.lua_state())) = &m_lua->hook;
    lua_sethook(lua.lua_state(), instructionHook, LUA_MASKCOUNT, kHookStep);

    sol::environment env(lua, sol::create, lua.globals());
    env["param"] = [this](lua_Integer id, const std::string &name, sol::this_state ts) -> sol::object {
        QVariant out;
        if (!m_host || !m_host->getParam
            || !m_host->getParam(quint64(id), QString::fromUtf8(name.c_str()), &out))
            throw std::runtime_error("node " + std::to_string(id) + " is not in this cue");
        return variantToLua(sol::state_view(ts.L), out);
    };
    m_lua->exprEnv = env;
}

void ScriptRuntime::teardownEngine() {
    m_lua.reset();
}

void ScriptRuntime::evaluateExpressions(const QVariantList &jobs, double t) {
    if (m_shuttingDown.load(std::memory_order_acquire) || !m_lua)
        return;
    sol::state &lua = m_lua->lua;
    sol::environment &env = m_lua->exprEnv;
    env["t"] = t;
    env["audio"] = bandsToLua(lua, m_host && m_host->audioBands ? m_host->audioBands(0) : QVariantMap{});

    QVariantList results;
    for (const QVariant &jv : jobs) {
        const QVariantMap job = jv.toMap();
        QVariantMap res{{"node", job.value("node")}, {"name", job.value("name")}, {"ok", false}};
        const std::string expr = job.value("expr").toString().toStdString();

        auto it = m_lua->exprCache.find(expr);
        QString error;
        if (it == m_lua->exprCache.end()) {
            sol::load_result lr = lua.load("return " + expr, "=expression", sol::load_mode::text);
            if (!lr.valid()) {
                sol::error e = lr;
                error = QString::fromUtf8(e.what());
            } else {
                if (m_lua->exprCache.size() > 256)
                    m_lua->exprCache.clear();
                sol::protected_function pf = lr;
                sol::set_environment(env, pf);
                it = m_lua->exprCache.emplace(expr, std::move(pf)).first;
            }
        }
        if (error.isEmpty()) {
            m_lua->hook.executed = 0;
            m_lua->hook.limit = kExpressionInstructionLimit;
            sol::protected_function_result r = it->second();
            if (!r.valid()) {
                sol::error e = r;
                error = QString::fromUtf8(e.what());
            } else if (r.get_type() == sol::type::number || r.get_type() == sol::type::boolean) {
                const double v = r.get_type() == sol::type::boolean ? (r.get<bool>() ? 1.0 : 0.0)
                                                                    : r.get<double>();
                if (std::isfinite(v)) {
                    res["ok"] = true;
                    res["value"] = v;
                } else {
                    error = tr("Expression result is not a finite number");
                }
            } else {
                error = tr("Expression must return a number");
            }
        }
        res["error"] = error;
        results.append(res);
    }
    emit expressionsEvaluated(results);
}
#else
void ScriptRuntime::setupEngine() {}
void ScriptRuntime::teardownEngine() {}
void ScriptRuntime::evaluateExpressions(const QVariantList &jobs, double) {
    QVariantList results;
    for (const QVariant &jv : jobs) {
        const QVariantMap job = jv.toMap();
        results.append(QVariantMap{{"node", job.value("node")}, {"name", job.value("name")},
                                   {"ok", false}, {"error", tr("Lua support was disabled at build time.")}});
    }
    emit expressionsEvaluated(results);
}
#endif

void ScriptRuntime::executeScript() {
#ifndef PRISM_HAVE_LUA
    if (m_shuttingDown.load(std::memory_order_acquire))
        return;
    m_lastError = tr("Lua support was disabled at build time.");
    emit executionFinished(false, m_lastError);
    return;
#else
    if (m_shuttingDown.load(std::memory_order_acquire))
        return;

    if (!m_lua) {
        m_lastError = QStringLiteral("Lua engine not initialized.");
        emit executionFinished(false, m_lastError);
        return;
    }

    m_lastError.clear();
    if (m_script.trimmed().isEmpty()) {
        publishPulse();
        emit executionFinished(true, QString());
        return;
    }

    sol::state &lua = m_lua->lua;

    // Expose upstream data as the global `input` table (empty when unconnected).
    if (m_inputJson.trimmed().isEmpty()) {
        lua["input"] = lua.create_table();
    } else {
        const QJsonDocument inDoc = QJsonDocument::fromJson(m_inputJson.toUtf8());
        if (inDoc.isObject())
            lua["input"] = jsonToLua(lua, inDoc.object());
        else if (inDoc.isArray())
            lua["input"] = jsonToLua(lua, inDoc.array());
        else
            lua["input"] = lua.create_table();
    }

    m_lua->hook.executed = 0;
    m_lua->hook.limit = m_instructionLimit;
    m_logDirty = false;

    sol::protected_function_result result = lua.safe_script(
        m_script.toUtf8().constData(),
        sol::script_pass_on_error, "=script");

    const bool shuttingDown = m_shuttingDown.load(std::memory_order_acquire);
    if (m_logDirty && !shuttingDown)
        emit logChanged(m_log);

    if (shuttingDown)
        return;

    if (!result.valid()) {
        sol::error err = result;
        m_lastError = QString::fromUtf8(err.what());
        emit executionFinished(false, m_lastError);
        return;
    }

    sol::object ret = result;
    if (!ret.valid() || ret.get_type() == sol::type::lua_nil) {
        writeOutput(QStringLiteral("{}"));
    } else if (ret.get_type() == sol::type::string) {
        writeOutput(QString::fromUtf8(ret.as<std::string>().c_str()));
    } else if (ret.get_type() == sol::type::table) {
        const QJsonValue val = luaToJsonValue(ret);
        const QJsonDocument doc(val.toObject());
        writeOutput(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
    } else {
        const QJsonObject obj{{QStringLiteral("value"), luaToJsonValue(ret)}};
        writeOutput(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
    }
    emit executionFinished(true, QString());
#endif
}
