#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "core/sources/SourceDescriptor.h"

/// Backend of the add-source actions (slideshow, camera, screen / window, canvas, NDI, phone
/// camera). Each add*() builds the descriptor and hands it to AppController; it returns the new
/// node id (0 on failure). Only the phone camera needs a dialog (pairing); the rest are added
/// with defaults by addDefault() and configured in the inspector.
class SourcePrompts : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool ndiAvailable READ ndiAvailable CONSTANT)
    Q_PROPERTY(bool webrtcAvailable READ webrtcAvailable CONSTANT)
    /// Screen / window capture needs the in-app picker (the Linux portal does its own).
    Q_PROPERTY(bool capturePickerNeeded READ capturePickerNeeded CONSTANT)
    Q_PROPERTY(bool webrtcPeerConnected READ webrtcPeerConnected NOTIFY webrtcPeerChanged)
    Q_PROPERTY(QString webrtcQrImage READ webrtcQrImage NOTIFY webrtcPeerChanged)
    Q_PROPERTY(bool webrtcUsesRelay READ webrtcUsesRelay NOTIFY webrtcPeerChanged)
    Q_PROPERTY(QString webrtcDefaultRelayUrl READ webrtcDefaultRelayUrl CONSTANT)

public:
    explicit SourcePrompts(QObject *parent = nullptr);

    bool ndiAvailable() const;
    bool webrtcAvailable() const;
    bool capturePickerNeeded() const;
    bool webrtcPeerConnected() const;
    QString webrtcQrImage() const { return m_webrtcQr; }
    bool webrtcUsesRelay() const { return m_webrtcRelay; }
    QString webrtcDefaultRelayUrl() const;

    /// kind: "camera" | "slideshow" | "canvas" | "ndi" | "screen" | "window". Adds the node with
    /// default settings (first camera / NDI source, Pictures folder, 16:9 canvas).
    Q_INVOKABLE quint64 addDefault(const QString &kind);

    Q_INVOKABLE QStringList screenLabels() const;
    Q_INVOKABLE QStringList windowLabels();

    Q_INVOKABLE quint64 addShader(const QString &code);

    Q_INVOKABLE QStringList webrtcInterfaces() const;
    Q_INVOKABLE int webrtcDefaultInterface() const;
    /// Opens a pairing session. relayUrl empty = LAN on interface @p interfaceIndex.
    /// Asynchronous (may ask for firewall access first); completion is reported by webrtcStarted.
    /// On success webrtcQrImage / webrtcPeerConnected describe the session.
    Q_INVOKABLE void webrtcStart(const QString &relayUrl, int interfaceIndex);
    Q_INVOKABLE quint64 webrtcFinish();
    Q_INVOKABLE void webrtcCancel();

signals:
    void webrtcPeerChanged();
    void webrtcStarted(bool ok, const QString &error);

private:
    quint64 addSlideshow(const QString &folder, int intervalSeconds);
    quint64 addCamera(int index);
    /// kind: "screen" | "window". On the portal platforms there is nothing to pick.
    quint64 addPortalCapture(const QString &kind);
    quint64 addScreen(int index);
    quint64 addWindow(int index);
    /// fill: "checkered" | "transparent" | "color".
    quint64 addCanvas(int width, int height, const QString &fill, const QColor &color);
    /// Blocks for about two seconds while the network is scanned.
    QStringList discoverNdi();
    quint64 addNdi(const QString &name);
    quint64 add(SourceDescriptor desc, const QImage &thumb);
#ifdef PRISM_HAVE_WEBRTC
    void webrtcCreateSession(const QString &relay, const QString &bind);
#endif

    QString m_webrtcToken;
    QString m_webrtcRelayUrl;
    QString m_webrtcQr;
    QImage m_webrtcThumb;
    bool m_webrtcRelay = false;
    bool m_peerHooked = false;
};
