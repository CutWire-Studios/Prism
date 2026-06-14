#include "OutputWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>

OutputWindow::OutputWindow(QWidget *parent)
    : QMainWindow(parent) {
    setWindowTitle("SwitchX - Output Monitor");
    setGeometry(100, 100, 800, 600);
    createUI();
}

OutputWindow::~OutputWindow() = default;

void OutputWindow::createUI() {
    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(centralWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    setCentralWidget(centralWidget);

    outputWidget = new VideoWidget(this);
    layout->addWidget(outputWidget);

    QHBoxLayout *controlsLayout = new QHBoxLayout();
    controlsLayout->setContentsMargins(8, 8, 8, 8);
    controlsLayout->setSpacing(8);
    controlsLayout->addStretch();

    fullscreenBtn = new QPushButton("🖵", this);
    fullscreenBtn->setMaximumWidth(50);
    controlsLayout->addWidget(fullscreenBtn);

    layout->addLayout(controlsLayout);

    connect(fullscreenBtn, &QPushButton::clicked, this, &OutputWindow::onFullscreenClicked);
}

void OutputWindow::onFullscreenClicked() {
    if (isFullScreen()) {
        showNormal();
        fullscreenBtn->setText("🖵");
    } else {
        showFullScreen();
        fullscreenBtn->setText("🖦");
    }
}
