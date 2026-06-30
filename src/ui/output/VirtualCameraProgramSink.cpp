#include "ui/output/VirtualCameraProgramSink.h"

#include <QSettings>

QString VirtualCameraProgramSink::name() const {
    return QStringLiteral("Virtual Camera");
}

bool VirtualCameraProgramSink::isActive() const {
    return m_active;
}

void VirtualCameraProgramSink::setDevicePath(const QString &path) {
    if (m_active) return;
    m_devicePath = path;

    QSettings settings;
    settings.beginGroup(QStringLiteral("virtualCamera"));
    settings.setValue(QStringLiteral("devicePath"), m_devicePath);
    settings.endGroup();
}

void VirtualCameraProgramSink::stop() {
    stopInternal();
}
