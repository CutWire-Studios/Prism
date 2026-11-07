vec4 effect(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 d = (uv - 0.5) * vec2(asp, 1.0);
    float f = 1.0 + p_strength * (dot(d, d) - 0.5);
    vec2 p = vec2(0.5) + vec2(d.x / asp, d.y) * f;
    if (p.x < -0.0001 || p.x > 1.0001 || p.y < -0.0001 || p.y > 1.0001) return vec4(0.0);
    return texture2D(u_input, clamp(p, 0.0, 1.0));
}
