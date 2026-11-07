#include "core/effects/LutLoader.h"

#include <QFile>
#include <QList>
#include <algorithm>
#include <cmath>

namespace Lut {

bool parseCube(const QByteArray &text, int &size, std::vector<float> &rgb) {
    size = 0;
    rgb.clear();
    float dmin[3] = {0.f, 0.f, 0.f};
    float dmax[3] = {1.f, 1.f, 1.f};

    const QList<QByteArray> lines = text.split('\n');
    for (const QByteArray &raw : lines) {
        const QByteArray line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith("TITLE"))
            continue;
        const QList<QByteArray> tok = line.simplified().split(' ');
        const QByteArray &head = tok.first();
        if (head == "LUT_1D_SIZE")
            return false;
        if (head == "LUT_3D_SIZE") {
            size = tok.size() > 1 ? tok.at(1).toInt() : 0;
            if (size < 2 || size > 256) return false;
            rgb.reserve(size_t(size) * size * size * 3);
            continue;
        }
        if ((head == "DOMAIN_MIN" || head == "DOMAIN_MAX") && tok.size() >= 4) {
            for (int i = 0; i < 3; ++i)
                (head == "DOMAIN_MIN" ? dmin : dmax)[i] = tok.at(i + 1).toFloat();
            continue;
        }
        if (tok.size() != 3) continue;
        bool okR = false, okG = false, okB = false;
        const float r = tok.at(0).toFloat(&okR);
        const float g = tok.at(1).toFloat(&okG);
        const float b = tok.at(2).toFloat(&okB);
        if (!okR || !okG || !okB) continue;
        const float v[3] = {r, g, b};
        for (int i = 0; i < 3; ++i) {
            const float span = dmax[i] - dmin[i];
            rgb.push_back(span > 0.f ? (v[i] - dmin[i]) / span : v[i]);
        }
    }
    return size >= 2 && rgb.size() == size_t(size) * size * size * 3;
}

QImage atlasFromLattice(int size, const std::vector<float> &rgb) {
    if (size < 2 || rgb.size() != size_t(size) * size * size * 3) return {};

    auto at = [&](int r, int g, int b, int c) {
        return rgb[((size_t(b) * size + g) * size + r) * 3 + c];
    };
    QImage img(kAtlas, kAtlas, QImage::Format_RGBA8888);
    const float scale = float(size - 1) / float(kSize - 1);
    for (int tile = 0; tile < kSize; ++tile) {
        const int ox = (tile % kTilesPerRow) * kSize;
        const int oy = (tile / kTilesPerRow) * kSize;
        const float fb = tile * scale;
        const int b0 = std::min(int(fb), size - 1), b1 = std::min(b0 + 1, size - 1);
        const float tb = fb - b0;
        for (int y = 0; y < kSize; ++y) {
            const float fg = y * scale;
            const int g0 = std::min(int(fg), size - 1), g1 = std::min(g0 + 1, size - 1);
            const float tg = fg - g0;
            uchar *dst = img.scanLine(oy + y) + ox * 4;
            for (int x = 0; x < kSize; ++x) {
                const float fr = x * scale;
                const int r0 = std::min(int(fr), size - 1), r1 = std::min(r0 + 1, size - 1);
                const float tr = fr - r0;
                for (int c = 0; c < 3; ++c) {
                    const float c00 = at(r0, g0, b0, c) * (1 - tr) + at(r1, g0, b0, c) * tr;
                    const float c10 = at(r0, g1, b0, c) * (1 - tr) + at(r1, g1, b0, c) * tr;
                    const float c01 = at(r0, g0, b1, c) * (1 - tr) + at(r1, g0, b1, c) * tr;
                    const float c11 = at(r0, g1, b1, c) * (1 - tr) + at(r1, g1, b1, c) * tr;
                    const float v = (c00 * (1 - tg) + c10 * tg) * (1 - tb)
                                  + (c01 * (1 - tg) + c11 * tg) * tb;
                    dst[x * 4 + c] = uchar(std::lround(std::clamp(v, 0.f, 1.f) * 255.f));
                }
                dst[x * 4 + 3] = 255;
            }
        }
    }
    return img;
}

QImage load(const QString &path) {
    if (path.isEmpty()) return {};

    if (path.endsWith(QLatin1String(".cube"), Qt::CaseInsensitive)) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {};
        int size = 0;
        std::vector<float> rgb;
        return parseCube(f.readAll(), size, rgb) ? atlasFromLattice(size, rgb) : QImage();
    }

    const QImage src = QImage(path).convertToFormat(QImage::Format_RGBA8888);
    if (src.isNull()) return {};
    const int w = src.width(), h = src.height();

    QImage opaque = src;
    for (int y = 0; y < h; ++y) {
        uchar *p = opaque.scanLine(y);
        for (int x = 0; x < w; ++x) p[x * 4 + 3] = 255;
    }
    if (w == kAtlas && h == kAtlas) return opaque;

    if (h >= 2 && h <= 256 && w == h * h) {
        const int n = h;
        std::vector<float> rgb(size_t(n) * n * n * 3);
        for (int b = 0; b < n; ++b)
            for (int g = 0; g < n; ++g) {
                const uchar *row = opaque.constScanLine(g);
                for (int r = 0; r < n; ++r)
                    for (int c = 0; c < 3; ++c)
                        rgb[((size_t(b) * n + g) * n + r) * 3 + c] = row[(b * n + r) * 4 + c] / 255.f;
            }
        return atlasFromLattice(n, rgb);
    }
    return {};
}

} // namespace Lut
