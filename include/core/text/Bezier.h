#pragma once

namespace prism {

// Cubic bezier on one axis.
inline double cubicBezier(double a, double b, double c, double d, double t)
{
    const double mt = 1.0 - t;
    return mt * mt * mt * a + 3.0 * mt * mt * t * b + 3.0 * mt * t * t * c + t * t * t * d;
}

// Curve parameter for a given x. Callers keep the x control points monotonic — handles are
// clamped so the curve cannot fold back — which makes it single-valued and bisectable.
inline double bezierParameterForX(double x0, double x1, double x2, double x3, double x)
{
    double lo = 0.0;
    double hi = 1.0;
    for (int i = 0; i < 24; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (cubicBezier(x0, x1, x2, x3, mid) < x)
            lo = mid;
        else
            hi = mid;
    }
    return 0.5 * (lo + hi);
}

} // namespace prism
