#include "core/sources/SharedSourcePool.h"

SharedSourcePool &SharedSourcePool::instance() {
    static SharedSourcePool pool;
    return pool;
}

std::shared_ptr<void> SharedSourcePool::acquireImpl(const QString &key,
                                                    const std::function<std::shared_ptr<void>()> &factory) {
    for (auto it = m_backends.begin(); it != m_backends.end(); ) {
        if (it->expired()) it = m_backends.erase(it);
        else ++it;
    }
    if (auto it = m_backends.find(key); it != m_backends.end())
        return it->lock();
    auto sp = factory();
    if (sp)
        m_backends.insert(key, sp);
    return sp;
}

int SharedSourcePool::liveCount() {
    int n = 0;
    for (const auto &w : std::as_const(m_backends))
        if (!w.expired()) ++n;
    return n;
}

MissingSource::MissingSource(Type type, QString name)
    : m_type(type)
    , m_frame(16, 9, QImage::Format_RGB888)
    , m_name(std::move(name))
{
    m_frame.fill(Qt::black);
}

SharedFrameSource::SharedFrameSource(Type type, Producer producer, QString name)
    : m_type(type)
    , m_producer(std::move(producer))
    , m_name(std::move(name))
{
}

bool SharedFrameSource::nextFrame() {
    const quint64 gen = m_producer.poll();
    if (gen == m_lastGen) return false;
    m_lastGen = gen;
    m_frame = m_producer.frame();
    return true;
}
