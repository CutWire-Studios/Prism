#pragma once

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QSlider>

class ClipCard : public QFrame {
    Q_OBJECT
public:
    explicit ClipCard(int index, QWidget *parent = nullptr);

    void loadClip(const QString &path, const QPixmap &thumbnail);
    void clearClip();
    void setActive(bool active);
    void setASelected(bool selected);
    void setBSelected(bool selected);

    QString clipPath() const { return m_clipPath; }
    bool isMuted() const { return m_muted; }
    int volume() const { return volumeSlider->value(); }
    bool isRepeat() const { return m_repeat; }
    double startTime() const { return m_startTime; }
    double endTime() const { return m_endTime; }

signals:
    void triggered(int index);
    void aButtonClicked(int index);
    void bButtonClicked(int index);

private slots:
    void onMuteClicked();
    void onRepeatClicked();
    void onEditClicked();
    void onAButtonClicked();
    void onBButtonClicked();

private:
    int m_index;
    QString m_clipPath;
    bool m_muted = false;
    bool m_repeat = false;
    double m_startTime = 0.0;
    double m_endTime = -1.0;

    QPushButton *thumbnailBtn;
    QLabel *titleLabel;
    QPushButton *muteBtn;
    QSlider *volumeSlider;
    QPushButton *repeatBtn;
    QPushButton *editBtn;
    QPushButton *aBtn;
    QPushButton *bBtn;
    bool m_aSelected = false;
    bool m_bSelected = false;
};
