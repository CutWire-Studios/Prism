#include "ui/common/Icons.h"

#include <QAbstractButton>
#include <QAction>
#include <QLabel>
#include <QFile>
#include <QHash>
#include <QSvgRenderer>
#include <QPainter>
#include <QApplication>
#include "ui/common/Theme.h"

namespace {

QByteArray svgSource(const char *name) {
    static QHash<QByteArray, QByteArray> cache;
    const QByteArray key(name);
    auto it = cache.find(key);
    if (it == cache.end()) {
        QFile f(QStringLiteral(":/icons/%1.svg").arg(QLatin1String(name)));
        QByteArray data;
        if (f.open(QIODevice::ReadOnly))
            data = f.readAll();
        else
            qWarning("Icons: missing icon resource '%s'", name);
        it = cache.insert(key, data);
    }
    return it.value();
}

QColor buttonIconColor(const QAbstractButton *b) {
    const auto &t = Theme::instance().tokens();
    return b->property("primary").toBool() ? t.primaryFg : t.textSecondary;
}

void retint() {
    const auto refresh = [](QObject *o, auto &&set) {
        const QVariant n = o->property("_iconName");
        if (!n.isValid())
            return;
        const QByteArray name = n.toByteArray();
        const int size = o->property("_iconSize").toInt();
        const QVariant c = o->property("_iconColor");
        set(name.constData(), size, c.isValid() ? c.value<QColor>() : QColor());
    };
    for (QWidget *w : QApplication::allWidgets()) {
        if (auto *b = qobject_cast<QAbstractButton *>(w))
            refresh(b, [b](const char *n, int s, const QColor &c) {
                b->setIcon(Icons::icon(n, s, c.isValid() ? c : buttonIconColor(b)));
            });
        for (QAction *a : w->actions())
            refresh(a, [a](const char *n, int s, const QColor &c) {
                a->setIcon(Icons::icon(n, s, c));
            });
    }
}

void ensureThemeConnection() {
    static bool connected = false;
    if (connected)
        return;
    connected = true;
    QObject::connect(&Theme::instance(), &Theme::changed, qApp, [] { retint(); });
}

void storeProps(QObject *o, const char *name, int size, const QColor &color) {
    o->setProperty("_iconName", QByteArray(name));
    o->setProperty("_iconSize", size);
    o->setProperty("_iconColor", color.isValid() ? QVariant::fromValue(color) : QVariant());
    ensureThemeConnection();
}
}

void Icons::init() {
}

QPixmap Icons::pixmap(const char *name, int size, const QColor &color) {
    const qreal dpr = qApp->devicePixelRatio();
    const int px = qRound(size * dpr);
    QPixmap pix(px, px);
    pix.setDevicePixelRatio(dpr);
    pix.fill(Qt::transparent);

    QByteArray svg = svgSource(name);
    if (svg.isEmpty())
        return pix;

    const QColor c = color.isValid() ? color : Theme::instance().tokens().textSecondary;
    QByteArray stroke = "stroke=\"" + c.name(QColor::HexRgb).toLatin1() + "\"";
    if (c.alpha() < 255)
        stroke += " stroke-opacity=\"" + QByteArray::number(c.alphaF(), 'f', 3) + "\"";
    svg.replace("stroke=\"currentColor\"", stroke);
    svg.replace("stroke-width=\"2\"", "stroke-width=\"1.75\"");

    QSvgRenderer renderer(svg);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    renderer.render(&p, QRectF(0, 0, size, size));
    return pix;
}

QIcon Icons::icon(const char *name, int size, const QColor &color) {
    return QIcon(pixmap(name, size, color));
}

void Icons::setIconText(QAbstractButton *button, const char *name, int pixelSize,
                                  const QColor &color) {
    if (!button)
        return;
    storeProps(button, name, pixelSize, color);
    button->setText(QString());
    button->setIcon(icon(name, pixelSize, color.isValid() ? color : buttonIconColor(button)));
    button->setIconSize(QSize(pixelSize, pixelSize));
}

void Icons::setLabelText(QLabel *label, const char *name, int pixelSize) {
    if (!label)
        return;
    label->setPixmap(pixmap(name, pixelSize, label->palette().windowText().color()));
}

void Icons::setPlayPause(QAbstractButton *button, bool playing, int pixelSize) {
    setIconText(button, playing ? Names::Pause : Names::PlayArrow, pixelSize);
}

void Icons::setActionIcon(QAction *action, const char *name, int size,
                                    const QColor &color) {
    if (!action)
        return;
    storeProps(action, name, size, color);
    action->setIcon(icon(name, size, color));
}

void Icons::drawCentered(QPainter &p, const QRectF &rect, const char *name,
                         int pixelSize, const QColor &color) {
    const QPointF topLeft = rect.center() - QPointF(pixelSize / 2.0, pixelSize / 2.0);
    p.drawPixmap(topLeft, pixmap(name, pixelSize, color));
}
