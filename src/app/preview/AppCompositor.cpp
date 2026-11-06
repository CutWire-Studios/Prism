#include "preview/AppCompositor.h"

#include <QJSEngine>
#include <QQmlEngine>

QPointer<Compositor> AppCompositor::s_compositor;
AppCompositor *AppCompositor::s_self = nullptr;

AppCompositor::AppCompositor(QObject *parent)
    : QObject(parent)
{
    s_self = this;
}

AppCompositor::~AppCompositor()
{
    if (s_self == this)
        s_self = nullptr;
}

void AppCompositor::setInstance(Compositor *compositor)
{
    if (s_compositor == compositor)
        return;
    s_compositor = compositor;
    if (s_self)
        emit s_self->compositorChanged();
}

AppCompositor *AppCompositor::create(QQmlEngine *, QJSEngine *)
{
    return new AppCompositor;
}
