#include "core/render/SkiaRender.h"

#include "core/render/SkiaFonts.h"
#include "core/render/SkiaRuntime.h"
#include "core/render/SkiaVectorResources.h"
#include "core/render/VectorPainter.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkStream.h"
#include "modules/svg/include/SkSVGDOM.h"
#include "modules/svg/include/SkSVGRenderContext.h"
#include "modules/svg/include/SkSVGSVG.h"
#include "modules/svg/include/SkSVGTypes.h"

#include <QtMath>

#include <list>
#include <mutex>

namespace prism {

namespace {

struct Entry
{
    QByteArray bytes;
    sk_sp<SkSVGDOM> dom;
    QSizeF size;
};

constexpr size_t kCacheSize = 8;

std::mutex g_mutex;
std::list<Entry> g_cache;

// Callers hold g_mutex. Parse failures are not cached.
Entry *entryFor(const QByteArray &svg)
{
    for (auto it = g_cache.begin(); it != g_cache.end(); ++it) {
        if (it->bytes == svg) {
            g_cache.splice(g_cache.begin(), g_cache, it);
            return &g_cache.front();
        }
    }
    SkMemoryStream stream(svg.constData(), size_t(svg.size()), false);
    sk_sp<SkSVGDOM> dom = SkSVGDOM::Builder()
                              .setFontManager(skia::systemFontMgr())
                              .setResourceProvider(skia::makeVectorResourceProvider(QString()))
                              .make(stream);
    if (!dom || !dom->getRoot())
        return nullptr;

    SkSVGSVG *root = dom->getRoot();
    QSizeF size;
    const SkSize intrinsic = root->intrinsicSize(SkSVGLengthContext(SkSize::Make(0, 0)));
    if (intrinsic.width() > 0 && intrinsic.height() > 0)
        size = QSizeF(intrinsic.width(), intrinsic.height());
    else if (root->getViewBox().has_value())
        size = QSizeF(root->getViewBox()->width(), root->getViewBox()->height());
    if (size.isEmpty())
        size = QSizeF(1280, 720);

    g_cache.push_front({svg, std::move(dom), size});
    if (g_cache.size() > kCacheSize)
        g_cache.pop_back();
    return &g_cache.front();
}

class SvgPainter final : public skia::VectorPainter
{
public:
    SvgPainter(sk_sp<SkSVGDOM> dom, QSizeF docSize, double scale)
        : m_dom(std::move(dom)), m_docSize(docSize), m_scale(scale)
    {
    }

    QSize size() const override
    {
        return {qMax(1, qCeil(m_docSize.width() * m_scale)), qMax(1, qCeil(m_docSize.height() * m_scale))};
    }
    quint64 cacheKey() const override { return 0; }
    void paint(SkCanvas &canvas) const override
    {
        canvas.scale(float(m_scale), float(m_scale));
        m_dom->setContainerSize(SkSize::Make(float(m_docSize.width()), float(m_docSize.height())));
        m_dom->render(&canvas);
    }

private:
    sk_sp<SkSVGDOM> m_dom;
    QSizeF m_docSize;
    double m_scale;
};

} // namespace

QSizeF svgIntrinsicSize(const QByteArray &svg)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    const Entry *e = entryFor(svg);
    return e ? e->size : QSizeF();
}

QImage renderSvg(const QByteArray &svg, double scale)
{
    if (svg.isEmpty() || scale <= 0.0)
        return {};
    std::lock_guard<std::mutex> lock(g_mutex);
    const Entry *e = entryFor(svg);
    if (!e)
        return {};
    return skia::rasterize(SvgPainter(e->dom, e->size, scale));
}

} // namespace prism
