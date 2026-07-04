#include "core/sources/HtmlWorkspace.h"
#include <algorithm>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace {

static const HtmlPresetInfo kPresets[] = {
    { QStringLiteral("clock"),
      QStringLiteral("Clock"),
      ":/html/widgets/clock.html",
      0.40f, 0.28f, 520, 220 },
    { QStringLiteral("countdown_timer"),
      QStringLiteral("Countdown Timer"),
      ":/html/widgets/countdown_timer.html",
      0.35f, 0.32f, 420, 260 },
    { QStringLiteral("cricket_score_bar"),
      QStringLiteral("Cricket Score Bar"),
      ":/html/widgets/cricket_score_bar.html",
      1.0f, 0.12f, 1280, 88 },
    { QStringLiteral("cricket_score_table"),
      QStringLiteral("Cricket Score Table"),
      ":/html/widgets/cricket_score_table.html",
      0.90f, 0.55f, 1200, 400 },
};

static QString loadResource(const char *path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll());
}

static QString escapeHtmlAttr(QString s) {
    s.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    s.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    s.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    s.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return s;
}

} // namespace

// ── HtmlWorkspaceComponent ────────────────────────────────────────────────────

QJsonObject HtmlWorkspaceComponent::toJson() const {
    QJsonObject o;
    o["id"]       = id;
    o["presetId"] = presetId;
    o["x"]        = (double)x;
    o["y"]        = (double)y;
    o["w"]        = (double)w;
    o["h"]        = (double)h;
    o["zIndex"]   = zIndex;
    o["visible"]  = visible;
    return o;
}

HtmlWorkspaceComponent HtmlWorkspaceComponent::fromJson(const QJsonObject &obj) {
    HtmlWorkspaceComponent c;
    c.id       = obj["id"].toString();
    c.presetId = obj["presetId"].toString();
    c.x        = static_cast<float>(obj["x"].toDouble(0.05));
    c.y        = static_cast<float>(obj["y"].toDouble(0.05));
    c.w        = static_cast<float>(obj["w"].toDouble(0.35));
    c.h        = static_cast<float>(obj["h"].toDouble(0.20));
    c.zIndex   = obj["zIndex"].toInt(0);
    c.visible  = obj["visible"].toBool(true);
    return c;
}

// ── HtmlWorkspace ─────────────────────────────────────────────────────────────

QJsonObject HtmlWorkspace::toJson() const {
    QJsonObject o;
    o["version"] = 1;
    o["canvasWidth"]  = kCanvasWidth;
    o["canvasHeight"] = kCanvasHeight;
    QJsonArray arr;
    for (const auto &c : components)
        arr.append(c.toJson());
    o["components"] = arr;
    return o;
}

HtmlWorkspace HtmlWorkspace::fromJson(const QJsonObject &obj) {
    HtmlWorkspace ws;
    for (const QJsonValue &v : obj["components"].toArray())
        ws.components.append(HtmlWorkspaceComponent::fromJson(v.toObject()));
    return ws;
}

HtmlWorkspace HtmlWorkspace::fromJsonString(const QString &json) {
    if (json.trimmed().isEmpty())
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject())
        return {};
    return fromJson(doc.object());
}

QString HtmlWorkspace::toJsonString() const {
    return QString::fromUtf8(QJsonDocument(toJson()).toJson(QJsonDocument::Compact));
}

// ── HtmlPresetRegistry ────────────────────────────────────────────────────────

const QList<HtmlPresetInfo> &HtmlPresetRegistry::presets() {
    static const QList<HtmlPresetInfo> list = [] {
        QList<HtmlPresetInfo> out;
        for (const auto &p : kPresets)
            out.append(p);
        return out;
    }();
    return list;
}

const HtmlPresetInfo *HtmlPresetRegistry::find(const QString &presetId) {
    for (const auto &p : kPresets) {
        if (p.id == presetId)
            return &p;
    }
    return nullptr;
}

QString HtmlPresetRegistry::loadFragment(const QString &presetId) {
    const HtmlPresetInfo *info = find(presetId);
    if (!info || !info->resourcePath)
        return {};
    return loadResource(info->resourcePath);
}

HtmlWorkspaceComponent HtmlPresetRegistry::makeComponent(const QString &presetId) {
    const HtmlPresetInfo *info = find(presetId);
    HtmlWorkspaceComponent c;
    c.id       = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.presetId = presetId;
    if (info) {
        c.w = info->defaultW;
        c.h = info->defaultH;
    }
    return c;
}

QString HtmlPresetRegistry::presetsAsJsonString() {
    QJsonArray arr;
    for (const auto &p : kPresets) {
        QJsonObject o;
        o["id"]         = p.id;
        o["name"]       = p.displayName;
        o["defaultW"]   = (double)p.defaultW;
        o["defaultH"]   = (double)p.defaultH;
        o["intrinsicW"] = p.intrinsicW;
        o["intrinsicH"] = p.intrinsicH;
        o["html"]       = loadResource(p.resourcePath);
        arr.append(o);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

// ── HtmlWorkspaceBuilder ──────────────────────────────────────────────────────

QString HtmlWorkspaceBuilder::build(const HtmlWorkspace &workspace) {
    QString body;

    QList<const HtmlWorkspaceComponent *> sorted;
    for (const auto &c : workspace.components) {
        if (c.visible)
            sorted.append(&c);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const HtmlWorkspaceComponent *a, const HtmlWorkspaceComponent *b) {
                  return a->zIndex < b->zIndex;
              });

    for (const HtmlWorkspaceComponent *c : sorted) {
        const HtmlPresetInfo *info = HtmlPresetRegistry::find(c->presetId);
        if (!info)
            continue;

        const QString fragment = HtmlPresetRegistry::loadFragment(c->presetId);
        if (fragment.isEmpty())
            continue;

        const int left = static_cast<int>(c->x * HtmlWorkspace::kCanvasWidth);
        const int top  = static_cast<int>(c->y * HtmlWorkspace::kCanvasHeight);
        const int w    = qMax(1, static_cast<int>(c->w * HtmlWorkspace::kCanvasWidth));
        const int h    = qMax(1, static_cast<int>(c->h * HtmlWorkspace::kCanvasHeight));

        const double sx = w / double(qMax(1, info->intrinsicW));
        const double sy = h / double(qMax(1, info->intrinsicH));

        body += QString(
            "<div class=\"wx-comp\" style=\"left:%1px;top:%2px;width:%3px;height:%4px;"
            "z-index:%5;\">"
            "<iframe scrolling=\"no\" style=\"width:%6px;height:%7px;"
            "transform:scale(%8,%9);\" srcdoc=\"%10\"></iframe>"
            "</div>\n")
                    .arg(left)
                    .arg(top)
                    .arg(w)
                    .arg(h)
                    .arg(c->zIndex)
                    .arg(info->intrinsicW)
                    .arg(info->intrinsicH)
                    .arg(sx, 0, 'f', 4)
                    .arg(sy, 0, 'f', 4)
                    .arg(escapeHtmlAttr(fragment));
    }

    return QString(R"(<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<style>
  * { margin: 0; padding: 0; box-sizing: border-box; }
  html, body {
    width: 1280px; height: 720px; overflow: hidden;
    background: transparent;
  }
  .wx-comp { position: absolute; overflow: hidden; }
  .wx-comp > iframe {
    display: block; border: 0; background: transparent;
    transform-origin: top left;
  }
</style>
</head>
<body>
%1
</body>
</html>)")
        .arg(body);
}

QString HtmlWorkspaceBuilder::buildFromJson(const QString &json) {
    return build(HtmlWorkspace::fromJsonString(json));
}
