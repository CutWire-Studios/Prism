#include "ui/output/MirrorOutputWindow.h"
#include "ui/output/ProgramMirrorWidget.h"
#include "ui/common/MaterialSymbols.h"
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPushButton>
#include <QScreen>
#include <QSpacerItem>
#include <QVBoxLayout>
#include <QWidget>

MirrorOutputWindow::MirrorOutputWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("CutWire Prism - Preview Output"));
    resize(800, 600);

    auto *central = new QWidget(this);
    auto *layout  = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_mirror = new ProgramMirrorWidget(central);
    m_mirror->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(m_mirror, 1);

    auto *controls = new QHBoxLayout();
    controls->setContentsMargins(8, 8, 8, 8);
    controls->addStretch();

    m_fullscreenBtn = new QPushButton(central);
    MaterialSymbols::setIconText(m_fullscreenBtn, MaterialSymbols::Names::Fullscreen, 22);
    m_fullscreenBtn->setMaximumWidth(50);
    connect(m_fullscreenBtn, &QPushButton::clicked, this, &MirrorOutputWindow::onFullscreenClicked);
    controls->addWidget(m_fullscreenBtn);

    layout->addLayout(controls);
    setCentralWidget(central);
}

void MirrorOutputWindow::setFrame(const QImage &frame) {
    if (m_mirror)
        m_mirror->setFrame(frame);
}

bool MirrorOutputWindow::isFullscreenActive() const {
#ifdef Q_OS_MACOS
    return m_fullscreen;
#else
    return isFullScreen();
#endif
}

void MirrorOutputWindow::updateFullscreenIcon() {
    MaterialSymbols::setIconText(m_fullscreenBtn,
        isFullscreenActive() ? MaterialSymbols::Names::CloseFullscreen
                             : MaterialSymbols::Names::Fullscreen, 22);
}

void MirrorOutputWindow::enterFullscreen() {
#ifdef Q_OS_MACOS
    // showFullScreen() only fills the area below the menu bar / notch on macOS,
    // so snap to the full screen geometry instead. See OutputWindow.
    if (QScreen *s = screen()) {
        m_normalGeometry = geometry();
        m_fullscreen = true;
        setGeometry(s->geometry());
        raise();
    }
#else
    showFullScreen();
#endif
    // Drive the icon off the actual state so a failed entry (e.g. screen() null)
    // leaves the button showing "enter fullscreen".
    updateFullscreenIcon();
}

void MirrorOutputWindow::exitFullscreen() {
#ifdef Q_OS_MACOS
    m_fullscreen = false;
    if (m_normalGeometry.isValid())
        setGeometry(m_normalGeometry);
#else
    showNormal();
#endif
    updateFullscreenIcon();
}

void MirrorOutputWindow::onFullscreenClicked() {
    if (isFullscreenActive())
        exitFullscreen();
    else
        enterFullscreen();
}

void MirrorOutputWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && isFullscreenActive()) {
        exitFullscreen();
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}
