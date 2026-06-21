#pragma once

/// Reference-counted wrapper around NDIlib_initialize / NDIlib_destroy so send
/// and receive can coexist without tearing down the SDK prematurely.
class NdiLibrary {
public:
    static NdiLibrary &instance();

    bool isAvailable() const;
    bool acquire();
    void release();

private:
    NdiLibrary() = default;

    int  m_refs         = 0;
    bool m_initialized  = false;
};
