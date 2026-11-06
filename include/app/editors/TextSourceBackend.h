#pragma once

#include "core/scripting/ScriptOutput.h"
#include "core/sources/SourceDescriptor.h"
#include "core/text/TextStyle.h"

#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <QtQml/qqmlregistration.h>

class QTimer;
class ClipGraphController;

// Backend of the Text source editor: the template, the prism::TextStyle it edits (exposed to QML as
// the normalised JSON the style editors bind to), the pack / look / animation galleries with their
// thumbnails, the live preview frame, and the script-variable binding of the Text node's DataIn.
class TextSourceBackend : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantMap style READ style NOTIFY styleChanged)
    Q_PROPERTY(QString templateHtml READ templateHtml NOTIFY templateChanged)
    Q_PROPERTY(QImage preview READ preview NOTIFY previewChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool scriptConnected READ scriptConnected NOTIFY scriptChanged)
    Q_PROPERTY(bool canRunScript READ canRunScript NOTIFY scriptChanged)
    Q_PROPERTY(QString scriptHint READ scriptHint NOTIFY scriptChanged)
    Q_PROPERTY(QVariantList scriptVariables READ scriptVariables NOTIFY scriptChanged)
    Q_PROPERTY(QVariantList userPacks READ userPacks NOTIFY packsChanged)
    Q_PROPERTY(QVariantList builtinPacks READ builtinPacks NOTIFY packsChanged)
    Q_PROPERTY(QVariantList lookThumbs READ lookThumbs NOTIFY packsChanged)
    Q_PROPERTY(QVariantList lookSpecs READ lookSpecs NOTIFY lookSpecsChanged)
    Q_PROPERTY(QVariantMap lookValues READ lookValues NOTIFY styleChanged)
    Q_PROPERTY(int animSlot READ animSlot WRITE setAnimSlot NOTIFY animListChanged)
    Q_PROPERTY(QString animCategory READ animCategory WRITE setAnimCategory NOTIFY animListChanged)
    Q_PROPERTY(QVariantList animCategories READ animCategories NOTIFY animListChanged)
    Q_PROPERTY(bool animCategoriesVisible READ animCategoriesVisible NOTIFY animListChanged)
    Q_PROPERTY(QVariantList animPresets READ animPresets NOTIFY animListChanged)
    Q_PROPERTY(QString animPresetId READ animPresetId NOTIFY animChanged)
    Q_PROPERTY(QVariantList animSpecs READ animSpecs NOTIFY animSpecsChanged)
    Q_PROPERTY(QVariantMap animValues READ animValues NOTIFY animChanged)
    Q_PROPERTY(QStringList animDisabledParams READ animDisabledParams NOTIFY animChanged)
    Q_PROPERTY(bool animCustom READ animCustom NOTIFY animChanged)
    Q_PROPERTY(QString animNote READ animNote NOTIFY animChanged)
    Q_PROPERTY(QStringList fontFamilies READ fontFamilies CONSTANT)

public:
    explicit TextSourceBackend(QObject *parent = nullptr);

    QVariantMap style() const;
    QString templateHtml() const;
    QImage preview() const { return m_preview; }
    bool playing() const { return m_playing; }
    bool scriptConnected() const { return m_binding.connected(); }
    bool canRunScript() const { return bool(m_binding.requestRun); }
    QString scriptHint() const;
    QVariantList scriptVariables() const;
    QVariantList userPacks() const { return m_userPacks; }
    QVariantList builtinPacks() const { return m_builtinPacks; }
    QVariantList lookThumbs() const { return m_lookThumbs; }
    QVariantList lookSpecs() const { return m_lookSpecs; }
    QVariantMap lookValues() const;
    int animSlot() const { return m_animSlot; }
    void setAnimSlot(int slot);
    QString animCategory() const { return m_animCategory; }
    void setAnimCategory(const QString &category);
    QVariantList animCategories() const { return m_animCategories; }
    bool animCategoriesVisible() const { return m_animCategoriesVisible; }
    QVariantList animPresets() const { return m_animPresets; }
    QString animPresetId() const;
    QVariantList animSpecs() const { return m_animSpecs; }
    QVariantMap animValues() const { return m_animValues; }
    QStringList animDisabledParams() const { return m_animDisabled; }
    bool animCustom() const { return m_animCustom; }
    QString animNote() const { return m_animNote; }
    QStringList fontFamilies() const;

    Q_INVOKABLE void open(ClipGraphController *graph, quint64 nodeId, QObject *app);
    Q_INVOKABLE QString templateText() const { return m_template; }
    Q_INVOKABLE void setTemplate(const QString &text);
    Q_INVOKABLE void setHighlightColors(const QColor &known, const QColor &unknown, const QColor &muted);
    Q_INVOKABLE void edit(const QVariantMap &patch);
    Q_INVOKABLE void setLayers(const QVariantList &layers);
    Q_INVOKABLE void applyLook(const QString &id);
    Q_INVOKABLE void setLookParam(const QString &id, const QVariant &value);
    Q_INVOKABLE void applyStylePack(const QString &id);
    Q_INVOKABLE void refreshPacks();
    Q_INVOKABLE bool saveStylePreset(const QString &name);
    Q_INVOKABLE QString packLabel(const QString &id) const;
    Q_INVOKABLE void renamePack(const QString &id, const QString &label);
    Q_INVOKABLE void deletePack(const QString &id);
    Q_INVOKABLE void applyAnimationPreset(const QString &presetId);
    Q_INVOKABLE void setAnimParam(const QString &id, const QVariant &value);
    Q_INVOKABLE void setPlaying(bool on);
    Q_INVOKABLE void runScript();
    Q_INVOKABLE QVariantList options(const QString &name) const;
    Q_INVOKABLE bool commit();

signals:
    void styleChanged();
    void templateChanged();
    void previewChanged();
    void playingChanged();
    void scriptChanged();
    void packsChanged();
    void animListChanged();
    void animSpecsChanged();
    void animChanged();
    void lookSpecsChanged();

private:
    prism::TextAnimationSlot &slotRef(int slot);
    QJsonObject styleJson() const;
    void styleEdited();
    void setScriptBinding(const ScriptBinding &binding);
    QStringList knownVariableNames() const;
    QString resolvedText() const;
    SourceDescriptor currentDescriptor() const;
    QImage cachedThumb(const QString &key, const std::function<QImage()> &render);
    void rebuildAnimList();
    void syncAnimTab();
    void syncLookSpecs();
    void scheduleRender();
    void updatePreview();
    void renderHeld();
    void renderPlayFrame();
    void refreshDurations();
    void onPollScriptOutput();

    ClipGraphController *m_graph = nullptr;
    quint64 m_nodeId = 0;
    prism::TextStyle m_style;
    QString m_template = QStringLiteral("Hello");
    int m_canvasW = 1280;
    int m_canvasH = 720;
    QImage m_preview;
    bool m_playing = false;
    bool m_durDirty = true;
    double m_inSec = 0.0;
    double m_outSec = 0.0;
    QElapsedTimer m_playClock;
    QTimer *m_renderTimer = nullptr;
    QTimer *m_playTimer = nullptr;
    QTimer *m_pollTimer = nullptr;

    ScriptBinding m_binding;
    QJsonObject m_vars;
    QStringList m_declaredVars;
    uint m_lastSeenVersion = 0;
    QColor m_knownColor = QColor(0x3d, 0xd6, 0x8c);
    QColor m_unknownColor = QColor(0xf5, 0xb1, 0x42);
    QColor m_mutedColor = QColor(0x9a, 0x9a, 0x9a);

    QHash<QString, QImage> m_thumbCache;
    QVariantList m_userPacks;
    QVariantList m_builtinPacks;
    QVariantList m_lookThumbs;

    int m_animSlot = 0;
    QString m_animCategory;
    QVariantList m_lookSpecs;
    QString m_lookSpecsId;
    QVariantList m_animCategories;
    bool m_animCategoriesVisible = false;
    QVariantList m_animPresets;
    QVariantList m_animSpecs;
    QString m_animSpecsKey;
    QVariantMap m_animValues;
    QStringList m_animDisabled;
    bool m_animCustom = false;
    QString m_animNote;
};
