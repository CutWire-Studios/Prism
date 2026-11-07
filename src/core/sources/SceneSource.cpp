#include "core/sources/SceneSource.h"

SceneSource::SceneSource(std::unique_ptr<MediaSource> content, QString name)
    : m_content(std::move(content)), m_name(std::move(name)), m_empty(2, 2, QImage::Format_RGBA8888)
{
    m_empty.fill(Qt::transparent);
}

bool SceneSource::nextFrame()
{
    if (!m_content)
        return false;
    const double dur = m_content->duration();
    if (dur <= 0.0)
        return m_content->nextFrame();

    if (!m_clock.isValid()) {
        m_anchor = m_content->currentTime();
        m_clock.start();
    }
    double desired = m_anchor + m_clock.elapsed() / 1000.0;
    if (desired >= dur) {
        m_content->seek(0.0);
        m_anchor = 0.0;
        m_clock.restart();
        desired = 0.0;
    }
    return m_content->presentAt(desired);
}
