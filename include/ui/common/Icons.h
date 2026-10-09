#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

class QAction;
class QAbstractButton;
class QLabel;
class QPainter;

/// Lucide SVG icons (resources/icons/<name>.svg), tinted and rasterised on demand.
class Icons {
public:
    Icons() = delete;

    static void init();

    static QPixmap pixmap(const char *name, int size,
                          const QColor &color = QColor());
    static QIcon icon(const char *name, int size = 16,
                      const QColor &color = QColor());

    static void setIconText(QAbstractButton *button, const char *name, int pixelSize = 24,
                            const QColor &color = QColor());
    static void setLabelText(QLabel *label, const char *name, int pixelSize = 24);
    static void setPlayPause(QAbstractButton *button, bool playing, int pixelSize = 20);
    static void setActionIcon(QAction *action, const char *name, int size = 16,
                              const QColor &color = QColor());

    static void drawCentered(QPainter &p, const QRectF &rect, const char *name,
                             int pixelSize, const QColor &color);

    struct Names {
        Names() = delete;
        inline static constexpr const char *Add = "plus";
        inline static constexpr const char *Check = "check";
        inline static constexpr const char *Close = "x";
        inline static constexpr const char *CloseFullscreen = "minimize-2";
        inline static constexpr const char *ContentCut = "scissors";
        inline static constexpr const char *CropSquare = "square";
        inline static constexpr const char *Delete = "trash-2";
        inline static constexpr const char *Description = "file-text";
        inline static constexpr const char *DesktopWindows = "monitor";
        inline static constexpr const char *Download = "download";
        inline static constexpr const char *ExpandLess = "chevron-up";
        inline static constexpr const char *ExpandMore = "chevron-down";
        inline static constexpr const char *Folder = "folder";
        inline static constexpr const char *FolderOpen = "folder-open";
        inline static constexpr const char *Fullscreen = "maximize-2";
        inline static constexpr const char *Grain = "sparkles";
        inline static constexpr const char *Inventory = "package";
        inline static constexpr const char *Language = "globe";
        inline static constexpr const char *Link = "link";
        inline static constexpr const char *Mic = "mic";
        inline static constexpr const char *Movie = "clapperboard";
        inline static constexpr const char *Pause = "pause";
        inline static constexpr const char *PhotoCamera = "camera";
        inline static constexpr const char *PlayArrow = "play";
        inline static constexpr const char *Remove = "minus";
        inline static constexpr const char *Repeat = "repeat";
        inline static constexpr const char *Save = "save";
        inline static constexpr const char *SelectWindow = "app-window";
        inline static constexpr const char *Sensors = "radio";
        inline static constexpr const char *SkipNext = "skip-forward";
        inline static constexpr const char *SkipPrevious = "skip-back";
        inline static constexpr const char *Smartphone = "smartphone";
        inline static constexpr const char *Speaker = "speaker";
        inline static constexpr const char *TextFields = "type";
        inline static constexpr const char *Visibility = "eye";
        inline static constexpr const char *VisibilityOff = "eye-off";
        inline static constexpr const char *VolumeOff = "volume-x";
        inline static constexpr const char *VolumeUp = "volume-2";
        inline static constexpr const char *Warning = "triangle-alert";
        inline static constexpr const char *ZoomIn = "zoom-in";
        inline static constexpr const char *ZoomOut = "zoom-out";
        inline static constexpr const char *ZoomReset = "rotate-ccw";
    };
};
