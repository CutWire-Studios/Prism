vec4 effect(vec2 uv) {
    vec4 src = texture2D(u_input, uv);
    vec2 asp = vec2(u_resolution.x / u_resolution.y, 1.0);
    vec2 p = (uv - 0.5) * asp;
    p = rot2(radians(p_rotation)) * p / p_zoom;
    p = p / asp + 0.5 - vec2(p_offsetX, p_offsetY);
    float inside = step(0.0, p.x) * step(p.x, 1.0) * step(0.0, p.y) * step(p.y, 1.0);
    vec4 fb = texture2D(u_prev, p);
    vec4 fp = vec4(fb.rgb * fb.a, fb.a) * (p_decay * inside);
    vec4 r = max(vec4(src.rgb * src.a, src.a), fp);
    return mix(src, vec4(r.rgb / max(r.a, 0.0001), r.a), p_mix);
}
