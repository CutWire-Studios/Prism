#include "ui/common/Theme.h"

#include "ui/common/Icons.h"

#include <QApplication>
#include <QCache>
#include <QDialog>
#include <QDockWidget>
#include <QEasingCurve>
#include <QEvent>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QMainWindow>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QRadialGradient>
#include <QSettings>
#include <QStyleHints>
#include <QVariantAnimation>
#include <QWidget>

#include <algorithm>
#include <utility>
#include <vector>

static bool hasBackdrop(const QWidget *w) {
    return qobject_cast<const QMainWindow *>(w) || qobject_cast<const QDialog *>(w)
           || qobject_cast<const QDockWidget *>(w);
}

namespace {

QColor rgba(const char *hex, qreal alpha) {
    QColor c(hex);
    c.setAlphaF(alpha);
    return c;
}

QString rgbaString(const QColor &c) {
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

class FadeOverlay : public QWidget {
public:
    FadeOverlay(const QPixmap &px, QWidget *parent) : QWidget(parent), m_px(px) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setGeometry(parent->rect());
        raise();
        show();
    }

    void setOpacity(qreal o) {
        m_opacity = o;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setOpacity(m_opacity);
        p.drawPixmap(rect(), m_px);
    }

private:
    QPixmap m_px;
    qreal m_opacity = 1.0;
};

QCache<QString, QPixmap> &backdropCache() {
    static QCache<QString, QPixmap> cache(8);
    return cache;
}

} // namespace

Theme &Theme::instance() {
    static Theme theme;
    return theme;
}

Theme::Theme() {
    const QString s = QSettings().value(QStringLiteral("appearance/mode")).toString();
    if (s == QLatin1String("light"))
        m_mode = Mode::Light;
    else if (s == QLatin1String("dark"))
        m_mode = Mode::Dark;
    m_dark = effectiveDark();
    rebuildTokens();

    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            &Theme::onSystemSchemeChanged);
    qApp->installEventFilter(this);
}

bool Theme::effectiveDark() const {
    switch (m_mode) {
    case Mode::Light: return false;
    case Mode::Dark: return true;
    case Mode::System: break;
    }
    return QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
}

void Theme::rebuildTokens() {
    Tokens t;
    if (m_dark) {
        t.bgBase = QColor("#0C0B11");
        t.bloomA = rgba("#6A4BE0", 0.22);
        t.bloomB = rgba("#2B2E8F", 0.18);
        t.glassPanel = rgba("#FFFFFF", 0.045);
        t.glassControl = rgba("#FFFFFF", 0.07);
        t.glassHover = rgba("#FFFFFF", 0.11);
        t.glassPressed = rgba("#FFFFFF", 0.04);
        t.stroke = rgba("#FFFFFF", 0.08);
        t.strokeTop = rgba("#FFFFFF", 0.14);
        t.surfacePopup = QColor("#1A1922");
        t.text = QColor("#F2F1F6");
        t.textSecondary = rgba("#F2F1F6", 0.62);
        t.textDisabled = rgba("#F2F1F6", 0.32);
        t.primaryBg = QColor("#F5F5F7");
        t.primaryFg = QColor("#111114");
        t.accent = QColor("#BCAEFF");
        t.accentSelection = rgba("#BCAEFF", 0.26);
        t.danger = QColor("#FF6B62");
        t.warning = QColor("#F2B544");
        t.success = QColor("#5BD08A");
    } else {
        t.bgBase = QColor("#F3F2F7");
        t.bloomA = rgba("#B7A6FF", 0.28);
        t.bloomB = rgba("#D9D2FF", 0.35);
        t.glassPanel = rgba("#FFFFFF", 0.55);
        t.glassControl = rgba("#FFFFFF", 0.75);
        t.glassHover = rgba("#FFFFFF", 0.90);
        t.glassPressed = rgba("#000000", 0.04);
        t.stroke = rgba("#000000", 0.08);
        t.strokeTop = rgba("#FFFFFF", 1.0);
        t.surfacePopup = QColor("#FBFAFD");
        t.text = QColor("#1B1A22");
        t.textSecondary = rgba("#1B1A22", 0.60);
        t.textDisabled = rgba("#1B1A22", 0.32);
        t.primaryBg = QColor("#1B1A22");
        t.primaryFg = QColor("#FFFFFF");
        t.accent = QColor("#7A64E8");
        t.accentSelection = rgba("#7A64E8", 0.18);
        t.danger = QColor("#D93A31");
        t.warning = QColor("#B87A0E");
        t.success = QColor("#22994F");
    }
    t.scrollHandle = t.text;
    t.scrollHandle.setAlphaF(0.18);
    t.scrollHandleHover = t.text;
    t.scrollHandleHover.setAlphaF(0.32);
    m_tokens = t;
}

void Theme::setMode(Mode mode) {
    if (mode == m_mode)
        return;
    m_mode = mode;
    const char *s = mode == Mode::Light ? "light" : mode == Mode::Dark ? "dark" : "system";
    QSettings().setValue(QStringLiteral("appearance/mode"), QString::fromLatin1(s));

    if (effectiveDark() != m_dark)
        applyAnimated();
}

void Theme::onSystemSchemeChanged() {
    if (m_mode == Mode::System && effectiveDark() != m_dark)
        applyAnimated();
}

void Theme::applyAnimated() {
    std::vector<FadeOverlay *> overlays;
    for (QWidget *w : QApplication::topLevelWidgets()) {
        if (!w->isVisible() || !hasBackdrop(w))
            continue;
        overlays.push_back(new FadeOverlay(w->grab(), w));
    }

    apply();

    for (FadeOverlay *o : overlays) {
        auto *anim = new QVariantAnimation(o);
        anim->setStartValue(1.0);
        anim->setEndValue(0.0);
        anim->setDuration(180);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(anim, &QVariantAnimation::valueChanged, o,
                [o](const QVariant &v) { o->setOpacity(v.toReal()); });
        connect(anim, &QVariantAnimation::finished, o, &QObject::deleteLater);
        anim->start();
    }
}

void Theme::apply() {
    m_dark = effectiveDark();
    rebuildTokens();
    backdropCache().clear();
    const Tokens &t = m_tokens;

    QString qss;
    {
        QFile f(QStringLiteral(":/styles/prism.qss"));
        if (f.open(QIODevice::ReadOnly))
            qss = QString::fromUtf8(f.readAll());
    }

    std::vector<std::pair<QString, QString>> subs = {
        {QStringLiteral("bgBase"), rgbaString(t.bgBase)},
        {QStringLiteral("bloomA"), rgbaString(t.bloomA)},
        {QStringLiteral("bloomB"), rgbaString(t.bloomB)},
        {QStringLiteral("glassPanel"), rgbaString(t.glassPanel)},
        {QStringLiteral("glassControl"), rgbaString(t.glassControl)},
        {QStringLiteral("glassHover"), rgbaString(t.glassHover)},
        {QStringLiteral("glassPressed"), rgbaString(t.glassPressed)},
        {QStringLiteral("stroke"), rgbaString(t.stroke)},
        {QStringLiteral("strokeTop"), rgbaString(t.strokeTop)},
        {QStringLiteral("surfacePopup"), rgbaString(t.surfacePopup)},
        {QStringLiteral("text"), rgbaString(t.text)},
        {QStringLiteral("textSecondary"), rgbaString(t.textSecondary)},
        {QStringLiteral("textDisabled"), rgbaString(t.textDisabled)},
        {QStringLiteral("primaryBg"), rgbaString(t.primaryBg)},
        {QStringLiteral("primaryFg"), rgbaString(t.primaryFg)},
        {QStringLiteral("accentSelection"), rgbaString(t.accentSelection)},
        {QStringLiteral("accent"), rgbaString(t.accent)},
        {QStringLiteral("danger"), rgbaString(t.danger)},
        {QStringLiteral("warning"), rgbaString(t.warning)},
        {QStringLiteral("success"), rgbaString(t.success)},
        {QStringLiteral("scrollHandleHover"), rgbaString(t.scrollHandleHover)},
        {QStringLiteral("scrollHandle"), rgbaString(t.scrollHandle)},
    };

    if (m_iconDir.isValid()) {
        const QColor checkColor = m_dark ? t.bgBase : QColor(Qt::white);
        const struct { const char *glyph; const char *token; QColor color; } icons[] = {
            {"check", "iconCheck", checkColor},
            {"chevron-down", "iconExpandMore", t.textSecondary},
            {"chevron-up", "iconExpandLess", t.textSecondary},
            {"minus", "iconRemove", t.textSecondary},
        };
        for (const auto &ic : icons) {
            QPixmap px = Icons::pixmap(ic.glyph, 32, ic.color);
            px.setDevicePixelRatio(2.0);
            const QString path = m_iconDir.filePath(
                QStringLiteral("%1_%2.png").arg(QLatin1String(ic.glyph)).arg(m_dark ? 'd' : 'l'));
            px.save(path, "PNG");
            subs.emplace_back(QLatin1String(ic.token), path);
        }
    }

    std::stable_sort(subs.begin(), subs.end(), [](const auto &a, const auto &b) {
        return a.first.size() > b.first.size();
    });
    for (const auto &s : subs)
        qss.replace(QLatin1Char('@') + s.first, s.second);

    qApp->setStyle(QStringLiteral("fusion"));

    QPalette pal;
    const QColor transparent(0, 0, 0, 0);
    pal.setColor(QPalette::Window, transparent);
    pal.setColor(QPalette::Base, transparent);
    pal.setColor(QPalette::AlternateBase, t.glassPanel);
    pal.setColor(QPalette::Button, t.glassControl);
    pal.setColor(QPalette::Text, t.text);
    pal.setColor(QPalette::WindowText, t.text);
    pal.setColor(QPalette::ButtonText, t.text);
    pal.setColor(QPalette::BrightText, t.text);
    pal.setColor(QPalette::Highlight, t.accentSelection);
    pal.setColor(QPalette::HighlightedText, t.text);
    pal.setColor(QPalette::ToolTipBase, t.surfacePopup);
    pal.setColor(QPalette::ToolTipText, t.text);
    pal.setColor(QPalette::PlaceholderText, t.textDisabled);
    pal.setColor(QPalette::Link, t.accent);
    pal.setColor(QPalette::Disabled, QPalette::Text, t.textDisabled);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, t.textDisabled);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, t.textDisabled);
    qApp->setPalette(pal);

    qApp->setStyleSheet(qss);

    emit changed();
}

void Theme::loadFonts() {
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/InterVariable.ttf"));
    QFont f = qApp->font();
    f.setFamily(QStringLiteral("Inter"));
    f.setPixelSize(13);
    qApp->setFont(f);
}

void Theme::paintBackdrop(QPainter &p, const QRect &rect) {
    if (rect.isEmpty())
        return;
    const qreal dpr = p.device()->devicePixelRatioF();
    const QString key = QStringLiteral("%1x%2@%3:%4")
                            .arg(rect.width()).arg(rect.height()).arg(dpr).arg(m_dark);
    QPixmap *cached = backdropCache().object(key);
    if (!cached) {
        auto *px = new QPixmap((rect.size() * dpr).expandedTo(QSize(1, 1)));
        px->setDevicePixelRatio(dpr);
        px->fill(m_tokens.bgBase);
        {
            QPainter bp(px);
            const qreal w = rect.width();
            const qreal h = rect.height();
            const auto bloom = [&](const QPointF &center, qreal radius, const QColor &c) {
                QRadialGradient g(center, radius);
                QColor clear = c;
                clear.setAlpha(0);
                g.setColorAt(0.0, c);
                g.setColorAt(1.0, clear);
                bp.fillRect(QRectF(0, 0, w, h), g);
            };
            bloom(QPointF(w * 0.15, 0), w * 0.55, m_tokens.bloomA);
            bloom(QPointF(w * 0.90, h), w * 0.45, m_tokens.bloomB);
        }
        backdropCache().insert(key, px);
        cached = px;
    }
    p.drawPixmap(rect.topLeft(), *cached);
}

bool Theme::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::Paint && watched->isWidgetType()) {
        auto *w = static_cast<QWidget *>(watched);
        if (w->isWindow() && hasBackdrop(w)) {
            QPainter p(w);
            paintBackdrop(p, w->rect());
        }
    }
    return false;
}
