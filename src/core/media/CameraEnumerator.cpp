#include "core/media/CameraEnumerator.h"

#include <QCameraDevice>
#include <QMediaDevices>
#include <QObject>

#include <algorithm>

#ifdef Q_OS_LINUX
#include <glob.h>
#endif

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>

#include <mutex>
#endif

namespace CameraEnumerator {

namespace {

#ifdef Q_OS_WIN
void ensureMediaFoundation() {
    static std::once_flag once;
    std::call_once(once, []() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        MFStartup(MF_VERSION, MFSTARTUP_LITE);
    });
}

void appendMediaFoundationCameras(QList<CameraDeviceInfo> &devices) {
    ensureMediaFoundation();

    IMFAttributes *attrs = nullptr;
    if (FAILED(MFCreateAttributes(&attrs, 1)))
        return;
    attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                   MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    IMFActivate **activates = nullptr;
    UINT32 count = 0;
    if (FAILED(MFEnumDeviceSources(attrs, &activates, &count))) {
        attrs->Release();
        return;
    }

    for (UINT32 i = 0; i < count; ++i) {
        WCHAR *symLink = nullptr;
        UINT32 symLen  = 0;
        WCHAR *friendly = nullptr;
        UINT32 friendlyLen = 0;

        activates[i]->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &symLink, &symLen);
        activates[i]->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &friendly, &friendlyLen);

        const QString id = symLink ? QString::fromWCharArray(symLink) : QString();
        QString label = friendly ? QString::fromWCharArray(friendly) : id;
        if (!label.isEmpty() && !id.isEmpty())
            label = QStringLiteral("%1  [%2]").arg(label, id);

        if (!id.isEmpty()) {
            const bool known = std::any_of(devices.cbegin(), devices.cend(),
                [&](const CameraDeviceInfo &e) {
                    return e.id.compare(id, Qt::CaseInsensitive) == 0;
                });
            if (!known)
                devices.append({id, label.isEmpty() ? id : label, false});
        }

        if (symLink)
            CoTaskMemFree(symLink);
        if (friendly)
            CoTaskMemFree(friendly);
        activates[i]->Release();
    }

    CoTaskMemFree(activates);
    attrs->Release();
}
#endif

#ifdef Q_OS_LINUX
void appendV4lDevices(QList<CameraDeviceInfo> &devices) {
    glob_t g{};
    if (::glob("/dev/video*", GLOB_NOSORT, nullptr, &g) != 0)
        return;

    for (size_t i = 0; i < g.gl_pathc; ++i) {
        const QString path = QString::fromLocal8Bit(g.gl_pathv[i]);
        const bool known = std::any_of(devices.cbegin(), devices.cend(),
            [&](const CameraDeviceInfo &e) { return e.id == path; });
        if (!known)
            devices.append({path, path, false});
    }
    ::globfree(&g);
}
#endif

} // namespace

QList<CameraDeviceInfo> listDevices() {
    const QList<QCameraDevice> qtDevices = QMediaDevices::videoInputs();

    QList<CameraDeviceInfo> devices;
    devices.reserve(qtDevices.size() + 4);

    for (const auto &d : qtDevices) {
        const QString id = QString::fromUtf8(d.id());
        const QString label = d.description().isEmpty() ? id
                            : QStringLiteral("%1  [%2]").arg(d.description(), id);
        devices.append({id, label, false});
    }

#ifdef Q_OS_LINUX
    appendV4lDevices(devices);
#elif defined(Q_OS_WIN)
    appendMediaFoundationCameras(devices);
#endif

    devices.append({QString(), QObject::tr("Default Camera  (let the system choose)"), true});
    return devices;
}

} // namespace CameraEnumerator
