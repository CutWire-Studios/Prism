#pragma once

#include <QList>
#include <QString>

// Which GPU the process runs on, for hybrid laptops. Windows only: on Linux that choice belongs to
// whoever launches Prism (prime-run, DRI_PRIME), not to Prism itself, so everything here is a
// no-op there and multipleAdapters() is false.
//
// GPU identity — what each adapter is, and which one OpenGL draws on — lives in
// core/media/GpuDevice.h.
namespace prism::gpu {

enum class Preference {
    Auto,            // whatever Windows and the driver decide
    PowerSaving,     // the integrated GPU
    HighPerformance, // the discrete GPU
};

// Stable ids for settings and QML: "auto", "integrated", "discrete". Unknown ids are Auto.
QString preferenceId(Preference preference);
Preference preferenceFromId(const QString &id);

Preference storedPreference();

// Windows with at least two hardware adapters.
bool multipleAdapters();

// Running from an MSIX package (the Microsoft Store build). Its HKCU writes land in a private
// hive the graphics driver never reads, so the choice has to be made in Windows Settings instead.
bool packagedApp();

// True where choosing in Prism changes anything: multipleAdapters() and not packagedApp().
bool preferenceSupported();

// Persists the choice ("graphics/preferredGpu") and writes it where Windows keeps per-application
// GPU preferences. The GPU is picked when the graphics driver loads, so this takes effect on the
// next launch.
void storePreference(Preference preference);

// For main(), before any graphics context exists. Re-asserts a stored non-Auto choice for the
// executable's current path — the registry is keyed on it, and an update that installs somewhere
// else would otherwise drop back to the Windows default — and returns the choice.
Preference applyStoredPreference();

} // namespace prism::gpu
