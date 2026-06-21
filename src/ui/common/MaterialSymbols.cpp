#include "ui/common/MaterialSymbols.h"

#include <QAbstractButton>
#include <QAction>
#include <QLabel>
#include <QFontDatabase>
#include <QPainter>

namespace {
QString g_family;
}

void MaterialSymbols::init() {
    if (!g_family.isEmpty())
        return;

    const int id = QFontDatabase::addApplicationFont(
        QStringLiteral(":/fonts/MaterialSymbolsRounded-Regular.ttf"));
    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    if (!families.isEmpty())
        g_family = families.first();
}

QFont MaterialSymbols::font(int pixelSize) {
    if (g_family.isEmpty())
        init();
    QFont f(g_family.isEmpty() ? QStringLiteral("Sans Serif") : g_family);
    f.setPixelSize(pixelSize);
    return f;
}

QPixmap MaterialSymbols::pixmap(const char *name, int size, const QColor &color) {
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(color);
    p.setFont(font(size));
    p.drawText(pix.rect(), Qt::AlignCenter, QString::fromUtf8(name));
    return pix;
}

QIcon MaterialSymbols::icon(const char *name, int size, const QColor &color) {
    return QIcon(pixmap(name, size, color));
}

void MaterialSymbols::setIconText(QAbstractButton *button, const char *name, int pixelSize) {
    if (!button)
        return;
    button->setFont(font(pixelSize));
    button->setText(QString::fromUtf8(name));
}

void MaterialSymbols::setLabelText(QLabel *label, const char *name, int pixelSize) {
    if (!label)
        return;
    label->setFont(font(pixelSize));
    label->setText(QString::fromUtf8(name));
}

void MaterialSymbols::setPlayPause(QAbstractButton *button, bool playing, int pixelSize) {
    setIconText(button, playing ? Names::Pause : Names::PlayArrow, pixelSize);
}

void MaterialSymbols::setActionIcon(QAction *action, const char *name, int size,
                                    const QColor &color) {
    if (!action)
        return;
    action->setIcon(icon(name, size, color));
}

void MaterialSymbols::drawCentered(QPainter &p, const QRectF &rect, const char *name,
                                   int pixelSize, const QColor &color) {
    p.save();
    p.setPen(color);
    p.setFont(font(pixelSize));
    p.drawText(rect, Qt::AlignCenter, QString::fromUtf8(name));
    p.restore();
}
