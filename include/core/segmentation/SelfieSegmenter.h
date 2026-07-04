#pragma once

#include <QImage>
#include <QByteArray>
#include <memory>

// Runs MediaPipe's Selfie Segmentation model (256x256 MobileNetV3) via ONNX
// Runtime to produce a foreground/background alpha matte for a single frame.
//
// The model is loaded from the Qt resource ":/models/selfie_segmentation.onnx".
// Inference is CPU-only and takes a few milliseconds per frame; call it off the
// UI/render thread (see SegmentationSource).
class SelfieSegmenter {
public:
    // Side length of the model's square input/output (pixels).
    static constexpr int kInputSize = 256;

    SelfieSegmenter();
    ~SelfieSegmenter();

    SelfieSegmenter(const SelfieSegmenter &) = delete;
    SelfieSegmenter &operator=(const SelfieSegmenter &) = delete;

    // True once the ONNX session loaded successfully.
    bool isReady() const { return m_ready; }

    // Compute the foreground mask for @p frameRgb. Returns a Format_Grayscale8
    // image (kInputSize x kInputSize) where 255 = foreground (keep) and
    // 0 = background (make transparent). Returns a null QImage on failure.
    QImage segment(const QImage &frameRgb);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_ready = false;
};
