#pragma once

#include "core/render/Compositor.h"

#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

struct CompositorForeign
{
    Q_GADGET
    QML_FOREIGN(Compositor)
    QML_NAMED_ELEMENT(Compositor)
    QML_UNCREATABLE("The compositor is owned by the application")
};

// Hands the application's single Compositor to QML. main.cpp calls setInstance() once the
// compositor exists; items bound to `AppCompositor.compositor` pick it up whenever that is.
class AppCompositor : public QObject
{
    Q_OBJECT
    QML_SINGLETON
    QML_ELEMENT

    Q_PROPERTY(Compositor *compositor READ compositor NOTIFY compositorChanged)

public:
    static void setInstance(Compositor *compositor);
    static AppCompositor *create(QQmlEngine *, QJSEngine *);

    Compositor *compositor() const { return s_compositor; }

signals:
    void compositorChanged();

private:
    explicit AppCompositor(QObject *parent = nullptr);
    ~AppCompositor() override;

    static QPointer<Compositor> s_compositor;
    static AppCompositor *s_self;
};
