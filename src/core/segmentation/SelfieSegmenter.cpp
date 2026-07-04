#include "core/segmentation/SelfieSegmenter.h"

#include <QFile>
#include <QDebug>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include <onnxruntime_cxx_api.h>

// Model I/O (see resources/models/selfie_segmentation.onnx):
//   input  "pixel_values" : float32 [1, 3, 256, 256], RGB, values in [0, 1]
//   output "alphas"       : float32 [1, 1, 256, 256], foreground probability
namespace {
constexpr int kSize = SelfieSegmenter::kInputSize;
constexpr const char *kModelResource = ":/models/selfie_segmentation.onnx";
}

struct SelfieSegmenter::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "PrismSelfieSegmenter"};
    Ort::SessionOptions options;
    std::unique_ptr<Ort::Session> session;
    Ort::AllocatorWithDefaultOptions allocator;

    QByteArray modelBytes;            // kept alive for the session lifetime
    std::string inputName;
    std::string outputName;

    std::vector<float> inputBuffer;   // 3 * kSize * kSize
    Ort::MemoryInfo memInfo =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
};

SelfieSegmenter::SelfieSegmenter() : m_impl(std::make_unique<Impl>()) {
    QFile f(kModelResource);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "SelfieSegmenter: cannot open model resource" << kModelResource;
        return;
    }
    m_impl->modelBytes = f.readAll();
    f.close();
    if (m_impl->modelBytes.isEmpty()) {
        qWarning() << "SelfieSegmenter: model resource is empty";
        return;
    }

    try {
        m_impl->options.SetIntraOpNumThreads(1);
        m_impl->options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        m_impl->session = std::make_unique<Ort::Session>(
            m_impl->env,
            m_impl->modelBytes.constData(),
            static_cast<size_t>(m_impl->modelBytes.size()),
            m_impl->options);

        auto in  = m_impl->session->GetInputNameAllocated(0, m_impl->allocator);
        auto out = m_impl->session->GetOutputNameAllocated(0, m_impl->allocator);
        m_impl->inputName  = in.get();
        m_impl->outputName = out.get();

        m_impl->inputBuffer.resize(static_cast<size_t>(3) * kSize * kSize);
        m_ready = true;
    } catch (const Ort::Exception &e) {
        qWarning() << "SelfieSegmenter: failed to create ONNX session:" << e.what();
        m_impl->session.reset();
        m_ready = false;
    }
}

SelfieSegmenter::~SelfieSegmenter() = default;

QImage SelfieSegmenter::segment(const QImage &frameRgb) {
    if (!m_ready || frameRgb.isNull())
        return {};

    // Preprocess: resize to model input and pack into planar CHW, scaled to [0,1].
    const QImage in = frameRgb.convertToFormat(QImage::Format_RGB888)
                          .scaled(kSize, kSize, Qt::IgnoreAspectRatio,
                                  Qt::SmoothTransformation);
    if (in.isNull())
        return {};

    float *buf = m_impl->inputBuffer.data();
    const int plane = kSize * kSize;
    for (int y = 0; y < kSize; ++y) {
        const uchar *line = in.scanLine(y);
        for (int x = 0; x < kSize; ++x) {
            const uchar r = line[x * 3 + 0];
            const uchar g = line[x * 3 + 1];
            const uchar b = line[x * 3 + 2];
            const int idx = y * kSize + x;
            buf[0 * plane + idx] = r / 255.0f;
            buf[1 * plane + idx] = g / 255.0f;
            buf[2 * plane + idx] = b / 255.0f;
        }
    }

    try {
        const std::array<int64_t, 4> shape{1, 3, kSize, kSize};
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            m_impl->memInfo, buf, m_impl->inputBuffer.size(),
            shape.data(), shape.size());

        const char *inNames[]  = {m_impl->inputName.c_str()};
        const char *outNames[] = {m_impl->outputName.c_str()};

        auto outputs = m_impl->session->Run(Ort::RunOptions{nullptr},
                                            inNames, &inputTensor, 1,
                                            outNames, 1);
        if (outputs.empty() || !outputs.front().IsTensor())
            return {};

        const float *mask = outputs.front().GetTensorData<float>();

        QImage out(kSize, kSize, QImage::Format_Grayscale8);
        for (int y = 0; y < kSize; ++y) {
            uchar *line = out.scanLine(y);
            for (int x = 0; x < kSize; ++x) {
                const float v = mask[y * kSize + x];
                const int a = static_cast<int>(std::lround(v * 255.0f));
                line[x] = static_cast<uchar>(std::clamp(a, 0, 255));
            }
        }
        return out;
    } catch (const Ort::Exception &e) {
        qWarning() << "SelfieSegmenter: inference failed:" << e.what();
        return {};
    }
}
