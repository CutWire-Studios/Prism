#pragma once

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QPixmap>
#include <QString>

class QAction;
class QAbstractButton;
class QLabel;
class QPainter;

/// Material Symbols Rounded icon font (ligature names from Google Fonts).
class MaterialSymbols {
public:
    MaterialSymbols() = delete;

    static void init();

    static QFont font(int pixelSize = 24);

    static QPixmap pixmap(const char *name, int size,
                          const QColor &color = QColor("#888888"));
    static QIcon icon(const char *name, int size = 16,
                      const QColor &color = QColor("#cccccc"));

    static void setIconText(QAbstractButton *button, const char *name, int pixelSize = 24);
    static void setLabelText(QLabel *label, const char *name, int pixelSize = 24);
    static void setPlayPause(QAbstractButton *button, bool playing, int pixelSize = 20);
    static void setActionIcon(QAction *action, const char *name, int size = 16,
                              const QColor &color = QColor("#cccccc"));

    static void drawCentered(QPainter &p, const QRectF &rect, const char *name,
                             int pixelSize, const QColor &color);

    struct Names {
        Names() = delete;
        inline static constexpr const char *Add = "add";
        inline static constexpr const char *Close = "close";
        inline static constexpr const char *CloseFullscreen = "close_fullscreen";
        inline static constexpr const char *ContentCut = "content_cut";
        inline static constexpr const char *CropSquare = "crop_square";
        inline static constexpr const char *Delete = "delete";
        inline static constexpr const char *Description = "description";
        inline static constexpr const char *DesktopWindows = "desktop_windows";
        inline static constexpr const char *Download = "download";
        inline static constexpr const char *Folder = "folder";
        inline static constexpr const char *FolderOpen = "folder_open";
        inline static constexpr const char *Fullscreen = "open_in_full";
        inline static constexpr const char *Grain = "grain";
        inline static constexpr const char *Inventory = "inventory_2";
        inline static constexpr const char *Language = "language";
        inline static constexpr const char *Movie = "movie";
        inline static constexpr const char *Pause = "pause";
        inline static constexpr const char *PhotoCamera = "photo_camera";
        inline static constexpr const char *PlayArrow = "play_arrow";
        inline static constexpr const char *Repeat = "repeat";
        inline static constexpr const char *Save = "save";
        inline static constexpr const char *SelectWindow = "select_window";
        inline static constexpr const char *Sensors = "sensors";
        inline static constexpr const char *SkipNext = "skip_next";
        inline static constexpr const char *SkipPrevious = "skip_previous";
        inline static constexpr const char *Smartphone = "smartphone";
        inline static constexpr const char *TextFields = "text_fields";
        inline static constexpr const char *Warning = "warning";
    };
};
