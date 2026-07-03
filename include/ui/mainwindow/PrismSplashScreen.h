#pragma once

#include <QSplashScreen>
#include <QPixmap>
#include <QString>

class PrismSplashScreen : public QSplashScreen {
    Q_OBJECT

public:
    explicit PrismSplashScreen(const QPixmap &pixmap = QPixmap());

    void setProgress(int percentage, const QString &statusText);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_progress = 0;
    QString m_statusText;
    QPixmap m_logo;
};
