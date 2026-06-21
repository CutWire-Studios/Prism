#include "core/sources/HtmlWorkspace.h"
#include <algorithm>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
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

static QString extractBodyInner(const QString &html) {
    static const QRegularExpression re(
        QStringLiteral("<body[^>]*>(.*)</body>"),
        QRegularExpression::CaseInsensitiveOption
        | QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch m = re.match(html);
    return m.hasMatch() ? m.captured(1).trimmed() : html;
}

static QString extractStyleBlocks(const QString &html) {
    static const QRegularExpression re(
        QStringLiteral("<style[^>]*>(.*?)</style>"),
        QRegularExpression::CaseInsensitiveOption
        | QRegularExpression::DotMatchesEverythingOption);
    QString out;
    auto it = re.globalMatch(html);
    while (it.hasNext())
        out += it.next().captured(1).trimmed() + QLatin1Char('\n');
    return out;
}

static QString extractScriptBlocks(const QString &html) {
    static const QRegularExpression re(
        QStringLiteral("<script[^>]*>(.*?)</script>"),
        QRegularExpression::CaseInsensitiveOption
        | QRegularExpression::DotMatchesEverythingOption);
    QString out;
    auto it = re.globalMatch(html);
    while (it.hasNext()) {
        const QString js = it.next().captured(1).trimmed();
        if (!js.isEmpty())
            out += QStringLiteral("<script>(function(){\n%1\n})();</script>\n").arg(js);
    }
    return out;
}

static QString scopeDomIds(QString text, const QString &scope) {
    static const QRegularExpression idAttr(QStringLiteral("id=\"([^\"]+)\""));
    text.replace(idAttr, QStringLiteral("id=\"%1-\\1\"").arg(scope));

    static const QRegularExpression getById(
        QStringLiteral(R"(getElementById\(\s*['"]([^'"]+)['"]\s*\))"));
    text.replace(getById, QStringLiteral("getElementById('%1-\\1')").arg(scope));
    return text;
}

static QString scopeCssIds(QString css, const QString &html, const QString &scope) {
    static const QRegularExpression idAttr(QStringLiteral("id=\"([^\"]+)\""));
    auto it = idAttr.globalMatch(html);
    while (it.hasNext()) {
        const QString id = it.next().captured(1);
        css.replace(QStringLiteral("#%1").arg(id),
                    QStringLiteral("#%1-%2").arg(scope, id));
    }
    return css;
}

struct ParsedFragment {
    QString styles;
    QString bodyHtml;
    QString scripts;
};

static ParsedFragment parseFragment(const QString &html, const QString &scope) {
    ParsedFragment p;
    p.styles   = scopeCssIds(extractStyleBlocks(html), html, scope);
    p.bodyHtml = scopeDomIds(extractBodyInner(html), scope);
    p.scripts  = scopeDomIds(extractScriptBlocks(html), scope);
    return p;
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

// ── HtmlWorkspaceBuilder ──────────────────────────────────────────────────────

QString HtmlWorkspaceBuilder::build(const HtmlWorkspace &workspace) {
    QString body;
    QString styles;
    QString scripts;
    int compIndex = 0;

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

        const QString scope = QStringLiteral("wx%1").arg(compIndex++);
        const ParsedFragment parsed = parseFragment(fragment, scope);

        const double sx = w / double(qMax(1, info->intrinsicW));
        const double sy = h / double(qMax(1, info->intrinsicH));

        styles += QStringLiteral("/* %1 */\n%2\n").arg(scope, parsed.styles);

        body += QString(
            "<div class=\"wx-comp\" style=\"left:%1px;top:%2px;width:%3px;height:%4px;"
            "z-index:%5;overflow:hidden;\">"
            "<div style=\"width:%6px;height:%7px;transform-origin:top left;"
            "transform:scale(%8,%9);\">"
            "%10"
            "</div></div>\n")
                    .arg(left)
                    .arg(top)
                    .arg(w)
                    .arg(h)
                    .arg(c->zIndex)
                    .arg(info->intrinsicW)
                    .arg(info->intrinsicH)
                    .arg(sx, 0, 'f', 4)
                    .arg(sy, 0, 'f', 4)
                    .arg(parsed.bodyHtml);

        scripts += parsed.scripts;
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
  .wx-comp { position: absolute; contain: layout paint; }
  .wx-comp > div { backface-visibility: hidden; }
%1</style>
</head>
<body>
%2
%3
</body>
</html>)")
        .arg(styles, body, scripts);
}

QString HtmlWorkspaceBuilder::buildFromJson(const QString &json) {
    return build(HtmlWorkspace::fromJsonString(json));
}
