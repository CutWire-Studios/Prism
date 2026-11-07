vec2 centered(vec2 uv) {
    return (uv - 0.5) * vec2(u_resolution.x / u_resolution.y, 1.0);
}

// 0..1 magnitude at x in 0..1 across the spectrum; a slow synthetic motion stands in until audio arrives.
float spectrumAt(float x) {
    x = clamp(x, 0.0, 1.0);
    if (!u_hasAudio) {
        float t = u_time;
        float v = 0.5 + 0.5 * sin(x * 11.0 + t * 2.1);
        v *= 0.55 + 0.45 * sin(x * 4.0 - t * 1.3);
        return clamp(v * (1.0 - 0.55 * x), 0.0, 1.0);
    }
    return texture2D(u_spectrum, vec2(x, 0.5)).r;
}

vec3 ramp3(vec3 a, vec3 b, vec3 m, bool useMid, float t) {
    t = clamp(t, 0.0, 1.0);
    if (!useMid)
        return mix(a, b, t);
    if (t < 0.5)
        return mix(a, m, t * 2.0);
    return mix(m, b, t * 2.0 - 1.0);
}

float triangleWave(float t) {
    return 1.0 - abs(1.0 - 2.0 * fract(t * 0.5));
}
