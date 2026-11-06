#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace RemoteProtocol {

struct RequestLine {
    QString method;
    QString path;
    bool    valid = false;
};

enum class Route {
    Root,
    Cam,
    Status,
    Fader,
    Cut,
    Auto,
    SelectA,
    SelectB,
    TogglePlayA,
    TogglePlayB,
    SetTransitionMode,
    SetDuration,
    NotFound,
    MethodNotAllowed,
};

struct ClipInfo {
    quint64 id = 0;
    QString name;
    bool    activeA = false;
    bool    activeB = false;
};

struct StatusData {
    int              fader = 0;
    bool             hasDeck = false;
    quint64          activeA = 0;
    quint64          activeB = 0;
    bool             isPlayingA = false;
    bool             isPlayingB = false;
    QVector<ClipInfo> clips;
    bool             hasTransition = false;
    int              transitionMode = 0;
    double           transitionDuration = 0.0;
    QStringList      transitionModes;
};

RequestLine parseRequestLine(const QString &raw);
Route       matchRoute(const QString &method, const QString &path);

bool intParam(const QString &path, const QString &key, int &out);
bool doubleParam(const QString &path, const QString &key, double &out);
bool uint64Param(const QString &path, const QString &key, quint64 &out);

QByteArray buildStatusJson(const StatusData &data);
QByteArray buildHttpResponse(int statusCode, const QByteArray &contentType, const QByteArray &body);

} // namespace RemoteProtocol
