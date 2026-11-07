float fbmOctaves(vec2 p) {
    float v = 0.0;
    float amp = 0.5;
    float total = 0.0;
    for (int i = 0; i < 6; ++i) {
        if (i >= p_octaves)
            break;
        v += amp * valueNoise(p);
        total += amp;
        p = p * 2.02 + vec2(17.0, 3.0);
        amp *= 0.5;
    }
    return v / max(total, 0.0001);
}

vec4 generate(vec2 uv) {
    float t = u_time * p_speed;
    vec2 p = rot2(t * 0.15) * centered(uv) * p_scale;
    p += vec2(t, t * 0.6);
    float n = fbmOctaves(p);
    n = clamp((n - 0.5) * p_contrast + 0.5, 0.0, 1.0);
    return vec4(mix(p_color_a.rgb, p_color_b.rgb, n), 1.0);
}
