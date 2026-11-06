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
    Take,
    Stage,
    LivePlay,
    LivePause,
    LiveToggle,
    LiveSeek,
    StagingPlay,
    StagingPause,
    StagingToggle,
    StagingSeek,
    SetTransitionMode,
    SetDuration,
    NotFound,
    MethodNotAllowed,
};

struct CueInfo {
    QString id;
    QString name;
    bool    onLive = false;
    bool    onStaging = false;
};

struct StatusData {
    int              fader = 0;
    bool             hasDeck = false;
    QString          live;
    QString          staging;
    bool             isPlayingLive = false;
    bool             isPlayingStaging = false;
    QVector<CueInfo> cues;
    bool             hasTransition = false;
    int              transitionMode = 0;
    double           transitionDuration = 0.0;
    QStringList      transitionModes;
};

RequestLine parseRequestLine(const QString &raw);
Route       matchRoute(const QString &method, const QString &path);

bool intParam(const QString &path, const QString &key, int &out);
bool doubleParam(const QString &path, const QString &key, double &out);
bool stringParam(const QString &path, const QString &key, QString &out);

QByteArray buildStatusJson(const StatusData &data);
QByteArray buildHttpResponse(int statusCode, const QByteArray &contentType, const QByteArray &body);

} // namespace RemoteProtocol
