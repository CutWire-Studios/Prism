#include "core/platform/ErrorReporter.h"

#include <QCoreApplication>
#include <QDebug>

namespace {
std::function<void(const QString &)> g_handler;
}

namespace ErrorReporter {

void setHandler(std::function<void(const QString &)> handler) { g_handler = std::move(handler); }

void report(const QString &message) {
    qWarning().noquote() << message;
    if (!g_handler || !QCoreApplication::instance())
        return;
    QMetaObject::invokeMethod(QCoreApplication::instance(), [message]() {
        if (g_handler)
            g_handler(message);
    }, Qt::QueuedConnection);
}

} // namespace ErrorReporter
