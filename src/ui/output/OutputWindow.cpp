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
    // showFullScreen() on a frameless window does not reliably cover the full
    // display (macOS menu bar / notch, Windows taskbar gaps). Snap to the screen
    // geometry instead and track state ourselves since isFullScreen() stays false.
    // The dynamic property lets the hosting VideoWidget disable window-drag while
    // fullscreen (it can't rely on isFullScreen() here).
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
    if (VideoWidget *vw = videoWidget())
        vw->update();
}

void OutputWindow::exitFullscreen() {
    m_fullscreen = false;
    setProperty("prismManualFullscreen", false);
    setWindowState(Qt::WindowNoState);
    if (m_normalGeometry.isValid())
        setGeometry(m_normalGeometry);
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
