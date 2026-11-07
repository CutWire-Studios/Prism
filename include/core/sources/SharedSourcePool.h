#pragma once

#include "core/sources/MediaSource.h"
#include <QHash>
#include <QImage>
#include <QString>
#include <functional>
#include <memory>

/// Process-wide registry of open capture backends keyed by physical device. Consumers hold a
/// shared_ptr, so a device is opened by the first acquire and closed when the last consumer
/// drops it. Expired entries are purged lazily.
class SharedSourcePool {
public:
    static SharedSourcePool &instance();

    /// Maps the logical device names a show uses to this venue's hardware.
    void setDeviceResolver(std::function<QString(const QString &)> resolver) { m_resolver = std::move(resolver); }
    QString resolve(const QString &logical) const { return m_resolver ? m_resolver(logical) : logical; }

    /// The live backend for `key`, or a fresh one from `factory`. Null if the factory fails;
    /// failures are not cached. A key must always be used with the same T.
    template <class T>
    std::shared_ptr<T> acquire(const QString &key, const std::function<std::shared_ptr<T>()> &factory) {
        return std::static_pointer_cast<T>(acquireImpl(key, [&] { return std::static_pointer_cast<void>(factory()); }));
    }

    int liveCount();

private:
    std::shared_ptr<void> acquireImpl(const QString &key, const std::function<std::shared_ptr<void>()> &factory);

    QHash<QString, std::weak_ptr<void>> m_backends;
    std::function<QString(const QString &)> m_resolver;
};

/// Black stand-in for a device that is absent or failed to open, so a scene still loads.
class MissingSource : public MediaSource {
public:
    MissingSource(Type type, QString name);

    Type    type()        const override { return m_type; }
    bool    isReady()     const override { return true; }
    bool    isMissing()   const override { return true; }
    QSize   frameSize()   const override { return m_frame.size(); }
    const uint8_t *frameData() const override { return m_frame.constBits(); }
    bool    nextFrame()         override { return false; }
    QString displayName() const override { return m_name; }

private:
    Type    m_type;
    QImage  m_frame;
    QString m_name;
};

/// Per-consumer view of a shared frame producer. Each consumer keeps its own copy of the latest
/// frame (implicitly shared) and pulls by generation, so it never touches GL state.
class SharedFrameSource : public MediaSource {
public:
    struct Producer {
        std::shared_ptr<void> keepAlive;
        std::function<quint64()> poll;          // current generation; may pump the backend
        std::function<const QImage &()> frame;
        std::function<bool()> missing;
    };

    SharedFrameSource(Type type, Producer producer, QString name);

    Type    type()        const override { return m_type; }
    bool    isReady()     const override { return !m_frame.isNull(); }
    bool    isMissing()   const override { return m_producer.missing && m_producer.missing(); }
    QSize   frameSize()   const override { return m_frame.size(); }
    const uint8_t *frameData() const override { return m_frame.constBits(); }
    bool    nextFrame()         override;
    QString displayName() const override { return m_name; }

private:
    Type     m_type;
    Producer m_producer;
    quint64  m_lastGen = 0;
    QImage   m_frame;
    QString  m_name;
};
