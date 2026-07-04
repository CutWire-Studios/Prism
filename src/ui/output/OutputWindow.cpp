#include "ui/output/OutputWindow.h"
#include "ui_OutputWindow.h"
#include "ui/canvas/VideoWidget.h"
#include <QAction>
#include <QKeyEvent>
#include <QMenu>
#include <QScreen>

OutputWindow::OutputWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::OutputWindow) {
    ui->setupUi(this);
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

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
    Qt::WindowFlags flags = Qt::Window | Qt::FramelessWindowHint;
    if (on)
        flags |= Qt::WindowStaysOnTopHint;
    setWindowFlags(flags);
    show();
}

VideoWidget *OutputWindow::videoWidget() const {
    return ui->outputWidget;
}

bool OutputWindow::isFullscreenActive() const {
#ifdef Q_OS_MACOS
    return m_fullscreen;
#else
    return isFullScreen();
#endif
}

void OutputWindow::enterFullscreen() {
#ifdef Q_OS_MACOS
    // showFullScreen() on a frameless window only fills the area below the menu
    // bar / notch (e.g. 1080 of a 1117px display), so it never truly covers the
    // screen. Snap to the full screen geometry instead — that does cover it —
    // and track state ourselves since isFullScreen() stays false this way. The
    // dynamic property lets the hosting VideoWidget disable window-drag while
    // fullscreen (it can't rely on isFullScreen() here).
    if (QScreen *s = screen()) {
        m_normalGeometry = geometry();
        m_fullscreen = true;
        setProperty("prismManualFullscreen", true);
        setGeometry(s->geometry());
        raise();
    }
#else
    showFullScreen();
#endif
}

void OutputWindow::exitFullscreen() {
#ifdef Q_OS_MACOS
    m_fullscreen = false;
    setProperty("prismManualFullscreen", false);
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

void OutputWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && isFullscreenActive()) {
        exitFullscreen();
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}
