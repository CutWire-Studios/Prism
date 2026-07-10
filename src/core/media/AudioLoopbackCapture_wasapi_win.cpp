#include "core/media/AudioLoopbackCaptureWasapi.h"
#include "core/media/AudioDecoder.h"
#include "core/media/PcmResampler.h"

#include <QDebug>

#include <mmdeviceapi.h>
#include <audioclient.h>
#include <combaseapi.h>

namespace AudioLoopbackWasapi {

namespace {

IMMDeviceEnumerator *g_enumerator = nullptr;
IMMDevice *g_device = nullptr;
IAudioClient *g_audioClient = nullptr;
IAudioCaptureClient *g_captureClient = nullptr;
WAVEFORMATEX *g_mixFormat = nullptr;
PcmResampler g_resampler;
bool g_needsResample = false;
bool g_comInitializedHere = false;

void releaseAll() {
    if (g_captureClient) { g_captureClient->Release(); g_captureClient = nullptr; }
    if (g_audioClient) { g_audioClient->Stop(); g_audioClient->Release(); g_audioClient = nullptr; }
    if (g_mixFormat) { CoTaskMemFree(g_mixFormat); g_mixFormat = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
    if (g_enumerator) { g_enumerator->Release(); g_enumerator = nullptr; }
    if (g_comInitializedHere) { CoUninitialize(); g_comInitializedHere = false; }
    g_needsResample = false;
    g_resampler.reset();
}

// WASAPI shared-mode mix formats are effectively always IEEE float on modern
// Windows (the audio engine's internal format); treat 32-bit as float rather
// than pulling in ksmedia.h just to compare KSDATAFORMAT_SUBTYPE_IEEE_FLOAT.
QAudioFormat::SampleFormat sampleFormatFromMixFormat(const WAVEFORMATEX *wfx) {
    switch (wfx->wBitsPerSample) {
    case 8:  return QAudioFormat::UInt8;
    case 16: return QAudioFormat::Int16;
    case 32: return QAudioFormat::Float;
    default: return QAudioFormat::Unknown;
    }
}

} // namespace

bool start(const QString &outputDeviceId) {
    stop();

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hr == RPC_E_CHANGED_MODE) {
        // COM already initialized on this thread (e.g. by Qt's platform
        // plugin) with a different concurrency model — still usable, but we
        // didn't add a reference, so we must not call CoUninitialize.
        g_comInitializedHere = false;
    } else if (SUCCEEDED(hr)) {
        g_comInitializedHere = true;
    } else {
        qWarning() << "AudioLoopbackWasapi: CoInitializeEx failed" << Qt::hex << hr;
        return false;
    }

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                           __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(&g_enumerator));
    if (FAILED(hr) || !g_enumerator) {
        qWarning() << "AudioLoopbackWasapi: failed to create device enumerator";
        releaseAll();
        return false;
    }

    if (!outputDeviceId.isEmpty()) {
        const std::wstring wid = outputDeviceId.toStdWString();
        hr = g_enumerator->GetDevice(wid.c_str(), &g_device);
        if (FAILED(hr) || !g_device) {
            qWarning() << "AudioLoopbackWasapi: GetDevice failed for" << outputDeviceId
                       << "- falling back to default output";
            g_device = nullptr;
        }
    }
    if (!g_device) {
        hr = g_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &g_device);
        if (FAILED(hr) || !g_device) {
            qWarning() << "AudioLoopbackWasapi: no default render endpoint available";
            releaseAll();
            return false;
        }
    }

    hr = g_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                             reinterpret_cast<void **>(&g_audioClient));
    if (FAILED(hr) || !g_audioClient) {
        qWarning() << "AudioLoopbackWasapi: failed to activate IAudioClient";
        releaseAll();
        return false;
    }

    hr = g_audioClient->GetMixFormat(&g_mixFormat);
    if (FAILED(hr) || !g_mixFormat) {
        qWarning() << "AudioLoopbackWasapi: GetMixFormat failed";
        releaseAll();
        return false;
    }

    constexpr REFERENCE_TIME kBufferDuration = 200 * 10000; // 200ms in 100ns units
    hr = g_audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                                    kBufferDuration, 0, g_mixFormat, nullptr);
    if (FAILED(hr)) {
        qWarning() << "AudioLoopbackWasapi: IAudioClient::Initialize failed" << Qt::hex << hr;
        releaseAll();
        return false;
    }

    hr = g_audioClient->GetService(__uuidof(IAudioCaptureClient),
                                    reinterpret_cast<void **>(&g_captureClient));
    if (FAILED(hr) || !g_captureClient) {
        qWarning() << "AudioLoopbackWasapi: failed to get IAudioCaptureClient";
        releaseAll();
        return false;
    }

    const QAudioFormat::SampleFormat inFmt = sampleFormatFromMixFormat(g_mixFormat);
    g_needsResample = !(inFmt == QAudioFormat::Float
                         && static_cast<int>(g_mixFormat->nSamplesPerSec) == AudioDecoder::kOutputSampleRate
                         && g_mixFormat->nChannels == AudioDecoder::kOutputChannels);
    if (g_needsResample) {
        if (inFmt == QAudioFormat::Unknown
            || !g_resampler.configure(static_cast<int>(g_mixFormat->nSamplesPerSec), g_mixFormat->nChannels, inFmt,
                                       AudioDecoder::kOutputSampleRate, AudioDecoder::kOutputChannels, QAudioFormat::Float)) {
            qWarning() << "AudioLoopbackWasapi: unable to configure resampler for mix format"
                       << g_mixFormat->nSamplesPerSec << "Hz" << g_mixFormat->nChannels << "ch"
                       << g_mixFormat->wBitsPerSample << "bit";
            releaseAll();
            return false;
        }
    }

    hr = g_audioClient->Start();
    if (FAILED(hr)) {
        qWarning() << "AudioLoopbackWasapi: IAudioClient::Start failed" << Qt::hex << hr;
        releaseAll();
        return false;
    }

    qInfo() << "AudioLoopbackWasapi: capturing loopback at" << g_mixFormat->nSamplesPerSec
            << "Hz" << g_mixFormat->nChannels << "ch" << g_mixFormat->wBitsPerSample << "bit";
    return true;
}

void stop() {
    releaseAll();
}

bool isRunning() {
    return g_audioClient != nullptr;
}

bool pull(QByteArray &pcmOut) {
    pcmOut.clear();
    if (!g_captureClient || !g_mixFormat)
        return false;

    UINT32 packetLength = 0;
    HRESULT hr = g_captureClient->GetNextPacketSize(&packetLength);
    if (FAILED(hr) || packetLength == 0)
        return false;

    QByteArray raw;
    while (packetLength != 0) {
        BYTE *data = nullptr;
        UINT32 numFrames = 0;
        DWORD flags = 0;
        hr = g_captureClient->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr);
        if (FAILED(hr))
            break;

        const int bytes = static_cast<int>(numFrames) * g_mixFormat->nBlockAlign;
        if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
            raw.append(bytes, '\0');
        else if (data)
            raw.append(reinterpret_cast<const char *>(data), bytes);

        g_captureClient->ReleaseBuffer(numFrames);

        hr = g_captureClient->GetNextPacketSize(&packetLength);
        if (FAILED(hr))
            break;
    }

    if (raw.isEmpty())
        return false;

    pcmOut = g_needsResample ? g_resampler.convert(raw) : raw;
    return !pcmOut.isEmpty();
}

} // namespace AudioLoopbackWasapi
