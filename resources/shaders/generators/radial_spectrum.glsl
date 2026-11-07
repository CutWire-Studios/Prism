vec4 generate(vec2 uv) {
    vec2 p = centered(uv);
    float r = length(p);
    float a = fract(atan(p.y, p.x) / 6.2831853 + 0.5 + u_time * p_spin);
    float n = float(p_bars);
    float x = (1.0 - abs(2.0 * a - 1.0)) * n;
    float id = floor(min(x, n - 0.001));
    float fx = fract(x);
    float v = clamp(spectrumAt((id + 0.5) / n) * p_gain, 0.0, 1.0);
    float len = p_length * v;
    float inBar = step(p_gap * 0.5, fx) * step(fx, 1.0 - p_gap * 0.5);
    float radial = smoothstep(p_radius - 0.003, p_radius, r) * (1.0 - smoothstep(p_radius + len, p_radius + len + 0.003, r));
    vec3 col = mix(p_color_low.rgb, p_color_high.rgb, clamp((r - p_radius) / max(p_length, 0.001), 0.0, 1.0));
    return vec4(mix(p_color_bg.rgb, col, inBar * radial), 1.0);
}
