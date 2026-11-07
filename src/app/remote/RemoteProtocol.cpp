#include "app/remote/RemoteProtocol.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>

namespace RemoteProtocol {

namespace {

QUrlQuery queryFromPath(const QString &path)
{
    return QUrlQuery(QUrl(path).query());
}

} // namespace

RequestLine parseRequestLine(const QString &raw)
{
    RequestLine result;
    const QStringList lines = raw.split("\r\n");
    if (lines.isEmpty())
        return result;

    const QStringList parts = lines.first().split(' ');
    if (parts.size() < 2)
        return result;

    result.method = parts[0];
    result.path   = parts[1];
    result.valid  = true;
    return result;
}

Route matchRoute(const QString &method, const QString &path)
{
    if (method != QStringLiteral("GET"))
        return Route::MethodNotAllowed;

    if (path == QStringLiteral("/"))
        return Route::Root;
    if (path.startsWith(QStringLiteral("/cam")))
        return Route::Cam;
    if (path == QStringLiteral("/api/status"))
        return Route::Status;
    if (path.startsWith(QStringLiteral("/api/fader")))
        return Route::Fader;
    if (path == QStringLiteral("/api/cut"))
        return Route::Cut;
    if (path == QStringLiteral("/api/take"))
        return Route::Take;

    const QString base = path.section(QLatin1Char('?'), 0, 0);
    if (base == QStringLiteral("/api/stage"))
        return Route::Stage;
    if (base == QStringLiteral("/api/scene/take"))
        return Route::SceneTake;
    if (base == QStringLiteral("/api/scene/next"))
        return Route::SceneNext;
    if (base == QStringLiteral("/api/scene/prev"))
        return Route::ScenePrev;
    if (base == QStringLiteral("/api/live/play"))
        return Route::LivePlay;
    if (base == QStringLiteral("/api/live/pause"))
        return Route::LivePause;
    if (base == QStringLiteral("/api/live/toggle"))
        return Route::LiveToggle;
    if (base == QStringLiteral("/api/live/seek"))
        return Route::LiveSeek;
    if (base == QStringLiteral("/api/staging/play"))
        return Route::StagingPlay;
    if (base == QStringLiteral("/api/staging/pause"))
        return Route::StagingPause;
    if (base == QStringLiteral("/api/staging/toggle"))
        return Route::StagingToggle;
    if (base == QStringLiteral("/api/staging/seek"))
        return Route::StagingSeek;
    if (path.startsWith(QStringLiteral("/api/setTransitionMode")))
        return Route::SetTransitionMode;
    if (path.startsWith(QStringLiteral("/api/setDuration")))
        return Route::SetDuration;

    return Route::NotFound;
}

bool intParam(const QString &path, const QString &key, int &out)
{
    const QUrlQuery query = queryFromPath(path);
    if (!query.hasQueryItem(key))
        return false;

    bool ok = false;
    const int val = query.queryItemValue(key).toInt(&ok);
    if (!ok)
        return false;

    out = val;
    return true;
}

bool doubleParam(const QString &path, const QString &key, double &out)
{
    const QUrlQuery query = queryFromPath(path);
    if (!query.hasQueryItem(key))
        return false;

    bool ok = false;
    const double val = query.queryItemValue(key).toDouble(&ok);
    if (!ok)
        return false;

    out = val;
    return true;
}

bool stringParam(const QString &path, const QString &key, QString &out)
{
    const QUrlQuery query = queryFromPath(path);
    if (!query.hasQueryItem(key))
        return false;

    out = query.queryItemValue(key);
    return true;
}

QByteArray buildStatusJson(const StatusData &data)
{
    QJsonObject statusObj;
    statusObj.insert(QStringLiteral("fader"), data.fader);

    if (data.hasDeck) {
        statusObj.insert(QStringLiteral("live"), data.live);
        statusObj.insert(QStringLiteral("staging"), data.staging);
        statusObj.insert(QStringLiteral("isPlayingLive"), data.isPlayingLive);
        statusObj.insert(QStringLiteral("isPlayingStaging"), data.isPlayingStaging);

        QJsonArray scenesArray;
        for (const SceneInfo &scene : data.scenes) {
            QJsonObject sceneObj;
            sceneObj.insert(QStringLiteral("id"), scene.id);
            sceneObj.insert(QStringLiteral("name"), scene.name);
            sceneObj.insert(QStringLiteral("onLive"), scene.onLive);
            sceneObj.insert(QStringLiteral("onStaging"), scene.onStaging);
            scenesArray.append(sceneObj);
        }
        statusObj.insert(QStringLiteral("scenes"), scenesArray);
    }

    if (data.hasTransition) {
        statusObj.insert(QStringLiteral("transitionMode"), data.transitionMode);
        statusObj.insert(QStringLiteral("transitionDuration"), data.transitionDuration);

        QJsonArray modesArray;
        for (const QString &modeName : data.transitionModes)
            modesArray.append(modeName);
        statusObj.insert(QStringLiteral("transitionModes"), modesArray);
    }

    return QJsonDocument(statusObj).toJson(QJsonDocument::Compact);
}

QByteArray buildHttpResponse(int statusCode, const QByteArray &contentType, const QByteArray &body)
{
    const QByteArray statusText = (statusCode == 200)
        ? QByteArrayLiteral("200 OK")
        : QByteArray::number(statusCode);

    return QByteArrayLiteral("HTTP/1.1 ") + statusText + "\r\n"
           "Content-Type: " + contentType + "\r\n"
           "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
           "Connection: close\r\n\r\n" + body;
}

} // namespace RemoteProtocol
