#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

struct HtmlWorkspaceComponent {
    QString id;       // unique instance id
    QString presetId; // e.g. "clock", "cricket_score_bar"
    float x = 0.05f;
    float y = 0.05f;
    float w = 0.35f;
    float h = 0.20f;
    int   zIndex = 0;
    bool  visible = true;

    QJsonObject toJson() const;
    static HtmlWorkspaceComponent fromJson(const QJsonObject &obj);
};

struct HtmlWorkspace {
    static constexpr int kCanvasWidth  = 1280;
    static constexpr int kCanvasHeight = 720;

    QList<HtmlWorkspaceComponent> components;

    bool isEmpty() const { return components.isEmpty(); }

    QJsonObject toJson() const;
    static HtmlWorkspace fromJson(const QJsonObject &obj);
    static HtmlWorkspace fromJsonString(const QString &json);

    QString toJsonString() const;
};

struct HtmlPresetInfo {
    QString id;
    QString displayName;
    const char *resourcePath = nullptr;
    float defaultW = 0.35f;
    float defaultH = 0.20f;
    int   intrinsicW = 400;
    int   intrinsicH = 200;
};

/// Catalog of the built-in HTML widget presets (clock, score bars, …) bundled
/// in resources. Resolves a preset id to its metadata, loads its HTML fragment,
/// and builds a default-placed HtmlWorkspaceComponent for it.
class HtmlPresetRegistry {
public:
    static const QList<HtmlPresetInfo> &presets();
    static const HtmlPresetInfo *find(const QString &presetId);
    static QString loadFragment(const QString &presetId);
    static HtmlWorkspaceComponent makeComponent(const QString &presetId);
};

class HtmlWorkspaceBuilder {
public:
    static QString build(const HtmlWorkspace &workspace);
    static QString buildFromJson(const QString &json);
};
