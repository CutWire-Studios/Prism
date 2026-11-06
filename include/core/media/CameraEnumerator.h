#pragma once

#include <QString>
#include <QList>


struct CameraDeviceInfo {
    QString id;
    QString label;
    bool    isDefault = false;
};

namespace CameraEnumerator {

/// Lists camera devices for UI pickers (Qt Multimedia + platform extras).
QList<CameraDeviceInfo> listDevices();

} // namespace CameraEnumerator
