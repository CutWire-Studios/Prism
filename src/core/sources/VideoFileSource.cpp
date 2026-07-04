#include "core/sources/VideoFileSource.h"
#include <QFileInfo>

VideoFileSource::VideoFileSource()
    : m_player(std::make_unique<VideoPlayer>()) {}

bool VideoFileSource::open(const QString &filePath) {
    m_name = QFileInfo(filePath).fileName();
    return m_player->open(filePath);
}

bool          VideoFileSource::isReady()     const { return m_player->isOpen(); }
QSize         VideoFileSource::frameSize()   const { return m_player->getFrameSize(); }
const uint8_t *VideoFileSource::frameData() const { return m_player->getFrameData(); }
int           VideoFileSource::frameBytesPerLine() const { return m_player->getFrameBytesPerLine(); }
bool          VideoFileSource::nextFrame()         { return m_player->decodeFrame(); }
double        VideoFileSource::duration()    const { return m_player->getDuration(); }
double        VideoFileSource::currentTime() const { return m_player->getCurrentTime(); }
void          VideoFileSource::seek(double s)      { m_player->seek(s); }
