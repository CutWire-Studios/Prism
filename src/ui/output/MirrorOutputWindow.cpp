#include "ui/output/MirrorOutputWindow.h"
#include "ui/output/ProgramMirrorWidget.h"
#include "ui/common/MaterialSymbols.h"
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPushButton>
#include <QGuiApplication>
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
    return m_fullscreen;
}

void MirrorOutputWindow::updateFullscreenIcon() {
    MaterialSymbols::setIconText(m_fullscreenBtn,
        isFullscreenActive() ? MaterialSymbols::Names::CloseFullscreen
                             : MaterialSymbols::Names::Fullscreen, 22);
}

void MirrorOutputWindow::enterFullscreen() {
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    // Geometry snap — showFullScreen() underfills frameless windows here
    // (macOS menu bar / notch, Windows taskbar gaps). See OutputWindow.
    QScreen *s = screen();
    if (!s)
        s = QGuiApplication::primaryScreen();
    if (s) {
        m_normalGeometry = geometry();
        m_fullscreen = true;
        setWindowState(Qt::WindowNoState);
        setGeometry(s->geometry());
        show();
        raise();
    }
#else
    // Let the WM handle fullscreen on Linux (KWin covers panels, Wayland
    // ignores client-set positions). See OutputWindow.
    m_fullscreen = true;
    showFullScreen();
    raise();
#endif
    // Drive the icon off the actual state so a failed entry (e.g. screen() null)
    // leaves the button showing "enter fullscreen".
    updateFullscreenIcon();
}

void MirrorOutputWindow::exitFullscreen() {
    m_fullscreen = false;
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    setWindowState(Qt::WindowNoState);
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
