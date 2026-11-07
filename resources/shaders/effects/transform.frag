vec4 effect(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 p = uv - p_offset - vec2(0.5);
    p.x *= asp;
    p = rot2(-radians(p_rotation)) * p;
    p /= max(p_scale, 0.0001);
    p.x /= asp;
    p += vec2(0.5);
    if (p.x < -0.0001 || p.x > 1.0001 || p.y < -0.0001 || p.y > 1.0001) return vec4(0.0);
    return texture2D(u_input, clamp(p, 0.0, 1.0));
}
