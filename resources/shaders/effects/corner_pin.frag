float pcross(vec2 x, vec2 y) {
    return x.x * y.y - x.y * y.x;
}

vec4 effect(vec2 uv) {
    vec2 a = p_tl;
    vec2 e = p_tr - a;
    vec2 f = p_bl - a;
    vec2 g = a - p_tr + p_br - p_bl;
    vec2 h = uv - a;
    float k2 = pcross(g, f);
    float k1 = pcross(e, f) + pcross(h, g);
    float k0 = pcross(h, e);
    float s;
    float t;
    if (abs(k2) < 0.001) {
        t = -k0 / k1;
        s = (h.x * k1 + f.x * k0) / (e.x * k1 - g.x * k0);
    } else {
        float w = sqrt(max(k1 * k1 - 4.0 * k0 * k2, 0.0));
        float ik2 = 0.5 / k2;
        t = (-k1 - w) * ik2;
        s = (h.x - f.x * t) / (e.x + g.x * t);
        if (!(s >= 0.0 && s <= 1.0 && t >= 0.0 && t <= 1.0)) {
            t = (-k1 + w) * ik2;
            s = (h.x - f.x * t) / (e.x + g.x * t);
        }
    }
    if (!(s >= -0.0001 && s <= 1.0001 && t >= -0.0001 && t <= 1.0001)) return vec4(0.0);
    return texture2D(u_input, clamp(vec2(s, t), 0.0, 1.0));
}
