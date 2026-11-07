vec4 effect(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 d = (uv - p_center) * vec2(asp, 1.0);
    float r = length(d);
    vec2 dir = r > 0.00001 ? d / r : vec2(0.0);
    float w = sin(r * p_frequency - u_time * p_speed) * p_amplitude;
    vec2 p = uv + vec2(dir.x / asp, dir.y) * w;
    if (p.x < -0.0001 || p.x > 1.0001 || p.y < -0.0001 || p.y > 1.0001) return vec4(0.0);
    return texture2D(u_input, clamp(p, 0.0, 1.0));
}
