#pragma once

#include "core/text/TextStyle.h"

#include <QList>
#include <QMutex>
#include <QString>

#include <optional>

namespace prism {

// Ids of user-saved presets carry this prefix so they can never collide with a built-in pack id,
// and so resolving one is a prefix test rather than a scan of both catalogs.
bool isUserTextPresetId(const QString &id);

// Text style packs the user saved from the inspector. Persisted per-user (not per-project) as a
// single JSON file, because the whole library is a handful of small objects and every mutation
// rewrites it anyway.
class TextPresetStore
{
public:
    static TextPresetStore &instance();

    // By value, and newest first: the preview provider resolves ids on the image-loading thread
    // while the GUI thread saves and deletes, so nothing may hand out a pointer into the library.
    QList<TextPreset> presets() const;
    std::optional<TextPreset> presetForId(const QString &id) const;

    // Returns the minted id, or an empty string when the label is blank.
    QString add(const QString &label, const TextStyle &style, const QString &sampleText);
    bool rename(const QString &id, const QString &label);
    bool remove(const QString &id);

    bool exportToFile(const QString &id, const QString &path) const;
    // Always mints a fresh id, so re-importing your own export adds a copy instead of clobbering.
    QString importFromFile(const QString &path);

    // Test seam: drops the in-memory copy so the next read re-reads the file.
    void reload();

    static QString storePath();

private:
    TextPresetStore() = default;

    void ensureLoaded() const;  // call with m_mutex held
    bool save() const;          // call with m_mutex held

    mutable QMutex m_mutex;
    mutable QList<TextPreset> m_presets;
    mutable bool m_loaded = false;
};

} // namespace prism
