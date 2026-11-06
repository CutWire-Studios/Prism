#pragma once

#include "core/text/TextAnimator.h"
#include "core/text/TextParamSpec.h"
#include "core/text/TextShading.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// Operations on a shading layer stack as the style editors see it: an array of layer objects in
// the textShadingLayerToJson shape with every key present. Each call returns a new array so the
// QML binds to the result instead of sharing mutable state.
class StyleLayers : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit StyleLayers(QObject *parent = nullptr);

    static QJsonObject mergePatch(QJsonObject base, const QJsonObject &patch);
    static QJsonObject normalizedLayer(const prism::TextShadingLayer &layer);
    static QJsonArray normalizedLayers(const QList<prism::TextShadingLayer> &layers);
    static QList<prism::TextShadingLayer> parseLayers(const QJsonArray &layers);

    static QString prettyId(const QString &id);
    static QVariantList specList(const QList<prism::TextAnimParamSpec> &specs, bool effectRules);
    static QVariantMap plainValues(const QMap<QString, prism::VectorSlotValue> &values);

    Q_INVOKABLE QVariantList add(const QVariantList &layers, const QString &kind) const;
    Q_INVOKABLE QVariantList duplicateAt(const QVariantList &layers, int index) const;
    Q_INVOKABLE QVariantList removeAt(const QVariantList &layers, int index) const;
    Q_INVOKABLE QVariantList moveAt(const QVariantList &layers, int index, int delta) const;
    Q_INVOKABLE QVariantList patchAt(const QVariantList &layers, int index, const QVariantMap &patch) const;
    Q_INVOKABLE QVariantList setPaintKindAt(const QVariantList &layers, int index, const QString &paintKind) const;
    Q_INVOKABLE QVariantList setEffectAt(const QVariantList &layers, int index, const QString &effectId) const;
    Q_INVOKABLE QVariantList setEffectParamAt(const QVariantList &layers, int index, const QString &paramId,
                                              const QVariant &value) const;
    Q_INVOKABLE QVariantList addGradientStopAt(const QVariantList &layers, int index, double pos) const;
    Q_INVOKABLE QVariantList removeGradientStopAt(const QVariantList &layers, int index, int stop) const;
    Q_INVOKABLE QVariantList setGradientStopAt(const QVariantList &layers, int index, int stop, double pos,
                                               const QString &color) const;

    Q_INVOKABLE QVariantList options(const QString &name) const;
    Q_INVOKABLE QString layerLabel(const QVariantMap &layer) const;
    Q_INVOKABLE QString localPath(const QUrl &url) const;
    Q_INVOKABLE QVariantList effectParams(const QString &effectId) const;
    Q_INVOKABLE QVariantMap effectValues(const QVariantList &layers, int index) const;
};
