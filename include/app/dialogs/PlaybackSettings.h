#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

/// Playback and recording options that the Performance settings page exposes beyond
/// PreferencesController: zero-copy upload, recording codec / container / encoder, and a
/// status line about the last frame upload.
class PlaybackSettings : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// "auto" | "on" | "off"; takes effect after a restart.
    Q_PROPERTY(QString zeroCopy READ zeroCopy WRITE setZeroCopy NOTIFY changed)
    /// Lists of {id, label}.
    Q_PROPERTY(QVariantList codecs READ codecs CONSTANT)
    Q_PROPERTY(QString codec READ codec WRITE setCodec NOTIFY changed)
    Q_PROPERTY(QVariantList containers READ containers CONSTANT)
    Q_PROPERTY(QString container READ container WRITE setContainer NOTIFY changed)
    /// Encoders available for the chosen codec.
    Q_PROPERTY(QVariantList encoders READ encoders NOTIFY changed)
    Q_PROPERTY(QString encoder READ encoder WRITE setEncoder NOTIFY changed)

public:
    explicit PlaybackSettings(QObject *parent = nullptr);

    QString zeroCopy() const;
    void setZeroCopy(const QString &mode);
    QVariantList codecs() const;
    QString codec() const;
    void setCodec(const QString &id);
    QVariantList containers() const;
    QString container() const;
    void setContainer(const QString &id);
    QVariantList encoders() const;
    QString encoder() const;
    void setEncoder(const QString &id);

    /// Multi-line text: last upload path, zero-copy decline reason, hardware fallbacks.
    Q_INVOKABLE QString uploadStatus() const;

signals:
    void changed();
};
