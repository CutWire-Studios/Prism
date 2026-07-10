#include "ui/output/OutputWindow.h"
#include "ui_OutputWindow.h"
#include "ui/canvas/VideoWidget.h"
#include <QAction>
#include <QKeyEvent>
#include <QMenu>
#include <QGuiApplication>
#include <QResizeEvent>
#include <QScreen>

OutputWindow::OutputWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::OutputWindow) {
    ui->setupUi(this);
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);

    ui->outputWidget->setFramelessWindowChrome(true);
    connect(ui->outputWidget, &VideoWidget::framelessToggleFullscreenRequested,
            this, [this]() { toggleFullscreen(); });
    connect(ui->outputWidget, &VideoWidget::framelessContextMenuRequested,
            this, [this](const QPoint &pos) { showContextMenu(pos); });
}

OutputWindow::~OutputWindow() {
    delete ui;
}

void OutputWindow::setRecordingActive(bool active) {
    ui->outputWidget->setStyleSheet(active
        ? QStringLiteral("background-color: #000; border: 3px solid #e04545;")
        : QStringLiteral("background-color: #000;"));
}

void OutputWindow::setStayOnTop(bool on) {
    const bool wasVisible = isVisible();
    Qt::WindowFlags flags = Qt::Window | Qt::FramelessWindowHint;
    if (on)
        flags |= Qt::WindowStaysOnTopHint;
    setWindowFlags(flags);
    if (wasVisible)
        show();
}

VideoWidget *OutputWindow::videoWidget() const {
    return ui->outputWidget;
}

bool OutputWindow::isFullscreenActive() const {
    return m_fullscreen;
}

void OutputWindow::enterFullscreen() {
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    // showFullScreen() on a frameless window does not reliably cover the full
    // display here (macOS menu bar / notch, Windows taskbar gaps). Snap to the
    // screen geometry instead and track state ourselves since isFullScreen()
    // stays false. The dynamic property lets the hosting VideoWidget disable
    // window-drag while fullscreen (it can't rely on isFullScreen() here).
    QScreen *s = screen();
    if (!s)
        s = QGuiApplication::primaryScreen();
    if (!s)
        return;

    m_normalGeometry = geometry();
    m_fullscreen = true;
    setProperty("prismManualFullscreen", true);
    setWindowState(Qt::WindowNoState);
    setGeometry(s->geometry());
    show();
    raise();
#else
    // On Linux/X11/Wayland the WM (e.g. KWin) must handle fullscreen: clients
    // can't set their own position on Wayland, and only showFullScreen() sets
    // _NET_WM_STATE_FULLSCREEN so panels are covered. A geometry snap leaves
    // the window under the panels.
    m_fullscreen = true;
    showFullScreen();
    raise();
#endif
    if (VideoWidget *vw = videoWidget())
        vw->update();
}

void OutputWindow::exitFullscreen() {
    m_fullscreen = false;
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    setProperty("prismManualFullscreen", false);
    setWindowState(Qt::WindowNoState);
    if (m_normalGeometry.isValid())
        setGeometry(m_normalGeometry);
#else
    showNormal();
#endif
}

void OutputWindow::toggleFullscreen() {
    if (isFullscreenActive())
        exitFullscreen();
    else
        enterFullscreen();
}

void OutputWindow::showContextMenu(const QPoint &globalPos) {
    QMenu menu(this);
    QAction *fullscreenAction = menu.addAction(
        isFullscreenActive() ? tr("Exit Full Screen") : tr("Full Screen"));
    fullscreenAction->setCheckable(true);
    fullscreenAction->setChecked(isFullscreenActive());

    if (menu.exec(globalPos) == fullscreenAction) {
        toggleFullscreen();
    }
}

void OutputWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if (ui->outputWidget)
        ui->outputWidget->update();
}

void OutputWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && isFullscreenActive()) {
        exitFullscreen();
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}
