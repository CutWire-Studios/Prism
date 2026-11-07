vec4 effect(vec2 uv) {
    vec2 q = uv * p_scale;
    float t = u_time * p_speed;
    float nx = fbm(q + vec2(t, 0.0));
    float ny = fbm(q + vec2(0.0, t) + vec2(5.2, 1.3));
    vec2 p = uv + (vec2(nx, ny) - 0.5) * 2.0 * p_amount;
    if (p.x < -0.0001 || p.x > 1.0001 || p.y < -0.0001 || p.y > 1.0001) return vec4(0.0);
    return texture2D(u_input, clamp(p, 0.0, 1.0));
}
