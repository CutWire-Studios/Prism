#include "ui/output/OutputWindow.h"
#include "ui_OutputWindow.h"
#include "ui/canvas/VideoWidget.h"
#include <QAction>
#include <QKeyEvent>
#include <QMenu>

OutputWindow::OutputWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::OutputWindow) {
    ui->setupUi(this);
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);

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

VideoWidget *OutputWindow::videoWidget() const {
    return ui->outputWidget;
}

void OutputWindow::toggleFullscreen() {
    if (isFullScreen()) {
        showNormal();
    } else {
        showFullScreen();
    }
}

void OutputWindow::showContextMenu(const QPoint &globalPos) {
    QMenu menu(this);
    QAction *fullscreenAction = menu.addAction(
        isFullScreen() ? tr("Exit Full Screen") : tr("Full Screen"));
    fullscreenAction->setCheckable(true);
    fullscreenAction->setChecked(isFullScreen());

    if (menu.exec(globalPos) == fullscreenAction) {
        toggleFullscreen();
    }
}

void OutputWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && isFullScreen()) {
        showNormal();
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}
