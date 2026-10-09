#include "core/render/SkiaFonts.h"

#include "core/render/SkiaRender.h"

#include "include/core/SkData.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkStream.h"
#include "include/core/SkString.h"
#include "include/core/SkTypeface.h"

#include <QDirIterator>
#include <QFile>
#include <QString>

#include <cstdlib>
#include <mutex>
#include <vector>

#if defined(_WIN32)
#include "include/ports/SkTypeface_win.h"
#elif defined(__APPLE__)
#include "include/ports/SkFontMgr_mac_ct.h"
#elif defined(SK_FONTMGR_FONTCONFIG_AVAILABLE)
#include "include/ports/SkFontMgr_fontconfig.h"
#include "include/ports/SkFontScanner_FreeType.h"
#elif defined(SK_FONTMGR_FREETYPE_EMPTY_AVAILABLE)
#include "include/ports/SkFontMgr_empty.h"
#endif

namespace prism::skia {

namespace {

sk_sp<SkFontMgr> makePlatformFontMgr()
{
    sk_sp<SkFontMgr> mgr;
#if defined(_WIN32)
    mgr = SkFontMgr_New_DirectWrite();
#elif defined(__APPLE__)
    mgr = SkFontMgr_New_CoreText(nullptr);
#elif defined(SK_FONTMGR_FONTCONFIG_AVAILABLE)
    mgr = SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType());
#elif defined(SK_FONTMGR_FREETYPE_EMPTY_AVAILABLE)
    mgr = SkFontMgr_New_Custom_Empty();
#endif
    return mgr ? mgr : SkFontMgr::RefEmpty();
}

// The system font manager with the application's bundled faces in front. Qt's
// addApplicationFont only registers with Qt on Windows and macOS, so Skia has to be handed the
// same files itself for SVG <text> to see them.
class BundledFontMgr final : public SkFontMgr
{
public:
    BundledFontMgr(sk_sp<SkFontMgr> base, std::vector<sk_sp<SkTypeface>> faces)
        : m_base(std::move(base)), m_faces(std::move(faces)) {}

protected:
    int onCountFamilies() const override { return m_base->countFamilies(); }
    void onGetFamilyName(int index, SkString *name) const override { m_base->getFamilyName(index, name); }
    sk_sp<SkFontStyleSet> onCreateStyleSet(int index) const override { return m_base->createStyleSet(index); }
    sk_sp<SkFontStyleSet> onMatchFamily(const char family[]) const override { return m_base->matchFamily(family); }

    sk_sp<SkTypeface> onMatchFamilyStyle(const char family[], const SkFontStyle &style) const override
    {
        if (sk_sp<SkTypeface> tf = bundled(family, style))
            return tf;
        return m_base->matchFamilyStyle(family, style);
    }

    sk_sp<SkTypeface> onMatchFamilyStyleCharacter(const char family[], const SkFontStyle &style,
                                                  const char *bcp47[], int bcp47Count,
                                                  SkUnichar character) const override
    {
        return m_base->matchFamilyStyleCharacter(family, style, bcp47, bcp47Count, character);
    }

    sk_sp<SkTypeface> onMakeFromData(sk_sp<SkData> data, int ttcIndex) const override
    {
        return m_base->makeFromData(std::move(data), ttcIndex);
    }
    sk_sp<SkTypeface> onMakeFromStreamIndex(std::unique_ptr<SkStreamAsset> stream, int ttcIndex) const override
    {
        return m_base->makeFromStream(std::move(stream), ttcIndex);
    }
    sk_sp<SkTypeface> onMakeFromStreamArgs(std::unique_ptr<SkStreamAsset> stream,
                                           const SkFontArguments &args) const override
    {
        return m_base->makeFromStream(std::move(stream), args);
    }
    sk_sp<SkTypeface> onMakeFromFile(const char path[], int ttcIndex) const override
    {
        return m_base->makeFromFile(path, ttcIndex);
    }

    sk_sp<SkTypeface> onLegacyMakeTypeface(const char family[], SkFontStyle style) const override
    {
        if (family) {
            if (sk_sp<SkTypeface> tf = bundled(family, style))
                return tf;
        }
        return m_base->legacyMakeTypeface(family, style);
    }

private:
    sk_sp<SkTypeface> bundled(const char family[], const SkFontStyle &want) const
    {
        if (!family)
            return nullptr;
        sk_sp<SkTypeface> best;
        int bestScore = 0;
        for (const sk_sp<SkTypeface> &face : m_faces) {
            SkString name;
            face->getFamilyName(&name);
            if (qstricmp(name.c_str(), family) != 0 && !isVariableAlias(name, family))
                continue;
            const SkFontStyle have = face->fontStyle();
            const int score = std::abs(have.weight() - want.weight())
                            + (have.slant() == want.slant() ? 0 : 1000);
            if (!best || score < bestScore) {
                best = face;
                bestScore = score;
            }
        }
        return best;
    }

    // InterVariable.ttf names itself "Inter Variable"; the app and its templates say "Inter".
    static bool isVariableAlias(const SkString &name, const char family[])
    {
        const QByteArray full = QByteArray(name.c_str());
        return full.endsWith(" Variable") && qstricmp(full.chopped(9).constData(), family) == 0;
    }

    sk_sp<SkFontMgr> m_base;
    std::vector<sk_sp<SkTypeface>> m_faces;
};

} // namespace

sk_sp<SkFontMgr> systemFontMgr()
{
    static sk_sp<SkFontMgr> mgr;
    static std::once_flag once;
    std::call_once(once, [] {
        sk_sp<SkFontMgr> base = makePlatformFontMgr();
        std::vector<sk_sp<SkTypeface>> faces;
        QDirIterator it(QStringLiteral(":/fonts"), {QStringLiteral("*.ttf")}, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QFile file(it.next());
            if (!file.open(QIODevice::ReadOnly))
                continue;
            const QByteArray bytes = file.readAll();
            if (sk_sp<SkTypeface> tf = base->makeFromData(SkData::MakeWithCopy(bytes.constData(), size_t(bytes.size()))))
                faces.push_back(std::move(tf));
        }
        mgr = sk_make_sp<BundledFontMgr>(std::move(base), std::move(faces));
    });
    return mgr;
}

} // namespace prism::skia

namespace prism {

bool svgFontAvailable(const QString &family)
{
    sk_sp<SkTypeface> tf = skia::systemFontMgr()->matchFamilyStyle(family.toUtf8().constData(), SkFontStyle());
    if (!tf)
        return false;
    SkString name;
    tf->getFamilyName(&name);
    return QString::fromUtf8(name.c_str()).startsWith(family, Qt::CaseInsensitive);
}

} // namespace prism
