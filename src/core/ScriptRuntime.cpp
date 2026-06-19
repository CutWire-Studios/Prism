#include "core/ScriptRuntime.h"

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

#ifdef SWITCHX_HAVE_LUA
extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}
#include <sol/sol.hpp>
#endif

namespace {

#ifdef SWITCHX_HAVE_LUA
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
#endif

} // namespace

ScriptRuntime::ScriptRuntime(std::shared_ptr<ScriptOutput> output, QObject *parent)
    : QObject(parent)
    , m_output(std::move(output))
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, &ScriptRuntime::onPeriodicTimeout);

    m_network = new QNetworkAccessManager(this);

#ifdef SWITCHX_HAVE_LUA
    setupEngine();
#endif
}

ScriptRuntime::~ScriptRuntime() {
    shutdown();
#ifdef SWITCHX_HAVE_LUA
    teardownEngine();
#endif
}

void ScriptRuntime::setScript(const QString &code) {
    m_script = code;
    if (m_triggerMode == ScriptTriggerMode::OnStart)
        runNow();
}

void ScriptRuntime::setTrigger(ScriptTriggerMode mode, int intervalMs) {
    m_triggerMode = mode;
    m_intervalMs = std::max(100, intervalMs);
    m_timer->stop();

    if (mode == ScriptTriggerMode::Periodic)
        m_timer->start(m_intervalMs);
    else if (mode == ScriptTriggerMode::OnStart)
        runNow();
}

void ScriptRuntime::applySettings(const QString &code, int triggerMode, int intervalMs) {
    m_script = code;
    setTrigger(static_cast<ScriptTriggerMode>(triggerMode), intervalMs);
}

void ScriptRuntime::runNow() {
    executeScript();
}

void ScriptRuntime::shutdown() {
    if (m_timer)
        m_timer->stop();
}

void ScriptRuntime::onPeriodicTimeout() {
    executeScript();
}

void ScriptRuntime::writeOutput(const QString &json, const QString &log) {
    if (!m_output)
        return;

    {
        QMutexLocker lock(&m_output->mutex);
        m_output->json = json;
    }
    m_output->version.fetch_add(1, std::memory_order_release);
    m_lastLog = log;
    emit executionFinished(true);
}

#ifdef SWITCHX_HAVE_LUA
struct ScriptRuntime::LuaState {
    sol::state lua;
};

void ScriptRuntime::setupEngine() {
    m_lua = std::make_unique<LuaState>();
    sol::state &lua = m_lua->lua;
    lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::string,
                       sol::lib::math, sol::lib::table, sol::lib::os);

    sol::table http = lua.create_named_table("http");
    http["get"] = [this](const std::string &urlStr, sol::optional<int> timeoutMs) -> std::string {
        const QUrl url(QString::fromUtf8(urlStr.c_str()));
        if (!url.isValid())
            return {};

        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("SwitchX/1.0"));
        QNetworkReply *reply = m_network->get(req);

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        const int ms = timeoutMs.value_or(10000);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        timer.start(ms);
        loop.exec();

        if (!reply->isFinished()) {
            reply->abort();
            reply->deleteLater();
            return {};
        }

        const QByteArray body = reply->readAll();
        reply->deleteLater();
        return std::string(body.constData(), static_cast<size_t>(body.size()));
    };
}

void ScriptRuntime::teardownEngine() {
    m_lua.reset();
}
#else
void ScriptRuntime::setupEngine() {}
void ScriptRuntime::teardownEngine() {}
#endif

void ScriptRuntime::executeScript() {
#ifndef SWITCHX_HAVE_LUA
    m_lastError = QStringLiteral("Lua support was disabled at build time.");
    emit executionFinished(false);
    return;
#else
    if (!m_lua) {
        m_lastError = QStringLiteral("Lua engine not initialized.");
        emit executionFinished(false);
        return;
    }

    if (m_script.trimmed().isEmpty()) {
        writeOutput(QStringLiteral("{}"), QStringLiteral("(empty script)"));
        return;
    }

    m_lastError.clear();
    sol::state &lua = m_lua->lua;

    sol::protected_function_result result = lua.safe_script(
        m_script.toUtf8().constData(),
        sol::script_pass_on_error);

    if (!result.valid()) {
        sol::error err = result;
        m_lastError = QString::fromUtf8(err.what());
        emit executionFinished(false);
        return;
    }

    sol::object ret = result;
    if (!ret.valid() || ret.get_type() == sol::type::lua_nil) {
        writeOutput(QStringLiteral("{}"), QStringLiteral("(no return value)"));
        return;
    }

    if (ret.get_type() == sol::type::string) {
        const QString json = QString::fromUtf8(ret.as<std::string>().c_str());
        writeOutput(json, json);
        return;
    }

    if (ret.get_type() == sol::type::table) {
        const QJsonValue val = luaToJsonValue(ret);
        const QJsonDocument doc(val.toObject());
        const QString json = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
        writeOutput(json, json);
        return;
    }

    const QString json = QStringLiteral("{\"value\":%1}")
                             .arg(QString::fromUtf8(ret.as<std::string>().c_str()));
    writeOutput(json, json);
#endif
}
