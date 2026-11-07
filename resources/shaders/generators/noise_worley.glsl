vec2 worley(vec2 x, float t) {
    vec2 n = floor(x);
    vec2 f = fract(x);
    float d1 = 8.0;
    float d2 = 8.0;
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            vec2 g = vec2(float(i), float(j));
            vec2 o = hash22(n + g);
            o = 0.5 + 0.5 * sin(t + 6.2831853 * o);
            vec2 r = g + o - f;
            float d = dot(r, r);
            if (d < d1) {
                d2 = d1;
                d1 = d;
            } else if (d < d2) {
                d2 = d;
            }
        }
    }
    return vec2(sqrt(d1), sqrt(d2));
}

vec4 generate(vec2 uv) {
    vec2 w = worley(centered(uv) * p_scale, u_time * p_speed);
    float v;
    if (p_style == 0)
        v = clamp(w.x, 0.0, 1.0);
    else
        v = smoothstep(0.0, 0.12, w.y - w.x);
    return vec4(mix(p_color_a.rgb, p_color_b.rgb, v), 1.0);
}
