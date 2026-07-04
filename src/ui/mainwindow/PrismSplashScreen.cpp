#include "ui/mainwindow/PrismSplashScreen.h"
#include "release.h"
#include <QPainter>
#include <QPaintEvent>
#include <QLinearGradient>

PrismSplashScreen::PrismSplashScreen(const QPixmap &pixmap)
    : QSplashScreen(pixmap)
{
    // Default transparent canvas of size 600x360
    QPixmap defaultPixmap(600, 360);
    defaultPixmap.fill(Qt::transparent);
    setPixmap(defaultPixmap);

    m_logo.load(QStringLiteral(":/Prism_icon.png"));
}

void PrismSplashScreen::setProgress(int percentage, const QString &statusText) {
    m_progress = percentage;
    m_statusText = statusText;
    showMessage(statusText); // forces repaint
}

void PrismSplashScreen::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 1. Draw Background with modern dark theme & cyan accent border
    QRect rect = this->rect();
    painter.fillRect(rect, QColor(21, 25, 30)); // #15191e

    QPen borderPen(QColor(0, 203, 214), 1); // Cyan border
    painter.setPen(borderPen);
    painter.drawRect(rect.adjusted(0, 0, -1, -1));

    // 2. Draw Logo
    if (!m_logo.isNull()) {
        painter.drawPixmap(40, 110, 110, 110, m_logo);
    }

    // 3. Draw Title
    painter.setPen(Qt::white);
    QFont titleFont = painter.font();
    titleFont.setFamily(QStringLiteral("Sans Serif"));
    titleFont.setPointSize(28);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.drawText(180, 150, QStringLiteral("CutWire Prism"));

    // 4. Draw Subtitle
    painter.setPen(QColor(136, 146, 176)); // #8892b0
    QFont subtitleFont = painter.font();
    subtitleFont.setPointSize(12);
    subtitleFont.setBold(false);
    painter.setFont(subtitleFont);
    painter.drawText(180, 185, QStringLiteral("Live Media Trigger & Control"));

    // 5. Draw Version
    painter.setPen(QColor(92, 103, 125)); // #5c677d
    QFont versionFont = painter.font();
    versionFont.setPointSize(10);
    painter.setFont(versionFont);
    painter.drawText(180, 210, QStringLiteral(PRISM_VERSION_STRING " (GPLv3)"));

    // 5b. Draw community/GitHub info
    painter.setPen(QColor(136, 146, 176)); // #8892b0
    QFont infoFont = painter.font();
    infoFont.setPointSize(10);
    painter.setFont(infoFont);
    painter.drawText(180, 240, QStringLiteral("Presented by CutWire Studios as a free, open-source project."));

    painter.setPen(QColor(0, 203, 214)); // Cyan URL link
    QFont urlFont = painter.font();
    urlFont.setPointSize(9);
    painter.setFont(urlFont);
    painter.drawText(180, 260, QStringLiteral("https://github.com/CutWire-Studios/Prism"));

    // 6. Draw Status Text
    painter.setPen(QColor(0, 253, 210)); // Neon cyan status text
    QFont statusFont = painter.font();
    statusFont.setPointSize(10);
    painter.setFont(statusFont);
    painter.drawText(40, 310, m_statusText);

    // 7. Draw sleek progress bar at the bottom
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(30, 35, 45)); // Background track
    painter.drawRect(40, 325, 520, 6);

    int fillWidth = (520 * m_progress) / 100;
    if (fillWidth > 0) {
        // Gradient fill for progress bar for premium look
        QLinearGradient grad(40, 325, 40 + fillWidth, 325);
        grad.setColorAt(0.0, QColor(0, 203, 214)); // Cyan
        grad.setColorAt(1.0, QColor(0, 253, 210)); // Neon Cyan glow
        painter.setBrush(grad);
        painter.drawRect(40, 325, fillWidth, 6);
    }
}
