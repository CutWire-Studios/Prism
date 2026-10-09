#pragma once

#include <QColor>
#include <QObject>
#include <QTemporaryDir>

class QPainter;
class QRect;
class QWidget;

class Theme : public QObject {
    Q_OBJECT
public:
    enum class Mode { System, Light, Dark };

    struct Tokens {
        QColor bgBase, bloomA, bloomB;
        QColor glassPanel, glassControl, glassHover, glassPressed;
        QColor stroke, strokeTop;
        QColor surfacePopup;
        QColor text, textSecondary, textDisabled;
        QColor primaryBg, primaryFg;
        QColor accent, accentSelection;
        QColor danger, warning, success;
        QColor scrollHandle, scrollHandleHover;
    };

    static Theme &instance();

    const Tokens &tokens() const { return m_tokens; }
    bool isDark() const { return m_dark; }
    Mode mode() const { return m_mode; }
    void setMode(Mode mode);

    void apply();
    static void loadFonts();
    void paintBackdrop(QPainter &p, const QRect &rect);

signals:
    void changed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    Theme();
    bool effectiveDark() const;
    void rebuildTokens();
    void applyAnimated();
    void onSystemSchemeChanged();

    Mode m_mode = Mode::System;
    bool m_dark = true;
    Tokens m_tokens;
    QTemporaryDir m_iconDir;
};
