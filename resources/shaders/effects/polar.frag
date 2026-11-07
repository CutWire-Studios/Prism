vec4 effect(vec2 uv) {
    if (p_mode == 0) {
        float asp = u_resolution.x / max(u_resolution.y, 1.0);
        vec2 d = (uv - 0.5) * vec2(asp, 1.0);
        float r = length(d) * 2.0;
        if (r > 1.0) return vec4(0.0);
        float ang = atan(d.y, d.x) / 6.2832;
        return texture2D(u_input, vec2(fract(ang), r));
    }
    float a = uv.x * 6.2832;
    float r = uv.y * 0.5;
    return texture2D(u_input, clamp(vec2(0.5) + r * vec2(cos(a), sin(a)), 0.0, 1.0));
}
