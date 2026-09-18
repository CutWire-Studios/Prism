#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

extern "C" {
#include <libavcodec/codec_id.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>
struct AVCodec;
}

// Hardware video device selection, shared by the preview decoder, the recorder and the
// preferences dialog so all three agree on what this machine can do and probe it only once.
namespace prism::hwaccel {

// Decode-side device backends. Exporter's HwBackend is a different axis — it names an
// encoder vendor family, and maps AMF onto a D3D11VA device.
enum class Backend { None, Cuda, D3d11va, Vaapi, VideoToolbox };

// Backends to try for decode on this platform, best first. CUDA leads where it exists
// because it is the only one of the three with both a fast readback and a scaler.
QList<Backend> decodeBackendOrder();

// The GPU Qt renders on, as its GL_VENDOR string. Seeded at startup from the same context
// probe that checks the driver's OpenGL version, and refreshed when the compositor comes up.
// decodeBackendOrder() uses it to keep decode on the device that will draw the frames; without
// it the order would depend on whether GL happened to be up when the first clip opened, and a
// reader latches its backend for good.
void setRenderVendor(const QString &vendor);

// The seeded render vendor. Empty until VideoWidget has initialised GL.
QString renderVendor();

// decodeBackendOrder() for an explicit renderer rather than the seeded one.
QList<Backend> decodeBackendOrderFor(const QString &renderVendor);

// Whether frames decoded on `backend` start out on the GPU `renderVendor` names. Mismatch is the
// only answer that changes behaviour: Unknown has to stay as permissive as an empty vendor, or a
// machine Drift cannot identify would lose hardware decode it can perfectly well do.
enum class RenderMatch { Matches, Mismatch, Unknown };

struct RenderMatchInfo
{
    RenderMatch match = RenderMatch::Unknown;
    QString decodeGpu; // human-readable, empty when the GPU could not be named
    QString renderGpu; // ditto
};

// The full verdict, with both GPUs named where they are known, for the picker and its warning.
// `renderVendor` empty means "ask renderVendor()".
RenderMatchInfo describeRenderMatch(Backend backend, const QString &renderVendor = {});

// describeRenderMatch() reduced to the predicate the decode-order logic wants. An empty vendor,
// and anything else this cannot pin down, matches everything.
bool backendMatchesRenderer(Backend backend, const QString &renderVendor);

// The backends ClipReader tries, in order. `pinnedOnly` is Hardware mode with a pin, which is
// honoured on its own: falling back to a backend the user did not choose would hide exactly the
// problem they picked around. Otherwise a pin leads only when it decodes on the rendering GPU —
// NVDEC pinned while GL draws on the integrated GPU is two PCIe crossings per frame, and CUDA-GL
// interop cannot apply — and the rest follows decodeBackendOrderFor().
QList<Backend> decodeAttemptOrder(Backend pinned, bool pinnedOnly, const QString &renderVendor);

// The device string av_hwdevice_ctx_create should get for `type`, empty for FFmpeg's default.
// For D3D11VA on Windows it is the DXGI index of the adapter the renderer is on: the default is
// adapter 0, which on a hybrid laptop is whichever GPU Windows lists first, not the one drawing —
// and D3D11-GL interop only works when the two are the same device.
QByteArray deviceString(AVHWDeviceType type);

// Those of decodeBackendOrder() whose device actually opens here, same order. This is
// what the preview's decode picker offers, so a listed choice is one that works.
QList<Backend> availableDecodeBackends();

AVHWDeviceType deviceType(Backend backend);

// Display name, in decode terms — the user is picking a decoder, not a device, so CUDA
// shows as NVDEC. Not translated: these are product names.
const char *name(Backend backend);

// Stable id for settings and QML ("nvdec", "vaapi", ...). backendFromId() resolves an
// unknown or absent id to None, which is what a config copied off another machine hits.
QString id(Backend backend);
Backend backendFromId(const QString &id);

// libavfilter scaler that runs on this backend's surfaces, or nullptr when there is none
// (D3D11VA, as of FFmpeg 7.1) — callers then read back at full size.
const char *scaleFilter(Backend backend);

// Cached: creating a device context is expensive, and the answer cannot change while the
// process runs.
bool deviceAvailable(AVHWDeviceType type);

// PRISM_NO_HWACCEL is the escape hatch for a driver that decodes garbage or crashes.
bool disabledByEnv();

// The first decoder for codecId that can drive `type`. avcodec_find_decoder() returns the
// preferred software decoder, which for AV1 is libdav1d — it has no hardware config at
// all, so looking at that codec alone would skip hardware the native `av1` decoder drives.
const AVCodec *findDecoder(AVCodecID codecId, AVHWDeviceType type, AVPixelFormat *pixFmt);

} // namespace prism::hwaccel
