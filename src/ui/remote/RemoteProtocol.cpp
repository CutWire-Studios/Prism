#include "ui/remote/RemoteProtocol.h"

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
    if (path == QStringLiteral("/api/auto"))
        return Route::Auto;
    if (path.startsWith(QStringLiteral("/api/selectA")))
        return Route::SelectA;
    if (path.startsWith(QStringLiteral("/api/selectB")))
        return Route::SelectB;
    if (path == QStringLiteral("/api/togglePlayA"))
        return Route::TogglePlayA;
    if (path == QStringLiteral("/api/togglePlayB"))
        return Route::TogglePlayB;
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

bool uint64Param(const QString &path, const QString &key, quint64 &out)
{
    const QUrlQuery query = queryFromPath(path);
    if (!query.hasQueryItem(key))
        return false;

    bool ok = false;
    const quint64 val = query.queryItemValue(key).toULongLong(&ok);
    if (!ok)
        return false;

    out = val;
    return true;
}

QByteArray buildStatusJson(const StatusData &data)
{
    QJsonObject statusObj;
    statusObj.insert(QStringLiteral("fader"), data.fader);

    if (data.hasDeck) {
        statusObj.insert(QStringLiteral("activeA"), QString::number(data.activeA));
        statusObj.insert(QStringLiteral("activeB"), QString::number(data.activeB));
        statusObj.insert(QStringLiteral("isPlayingA"), data.isPlayingA);
        statusObj.insert(QStringLiteral("isPlayingB"), data.isPlayingB);

        QJsonArray clipsArray;
        for (const ClipInfo &clip : data.clips) {
            QJsonObject clipObj;
            clipObj.insert(QStringLiteral("id"), QString::number(clip.id));
            clipObj.insert(QStringLiteral("name"), clip.name);
            clipObj.insert(QStringLiteral("activeA"), clip.activeA);
            clipObj.insert(QStringLiteral("activeB"), clip.activeB);
            clipsArray.append(clipObj);
        }
        statusObj.insert(QStringLiteral("clips"), clipsArray);
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
