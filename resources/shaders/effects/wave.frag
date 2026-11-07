vec4 effect(vec2 uv) {
    float ph = u_time * p_speed;
    vec2 p = uv;
    if (p_direction == 0) {
        p.x += sin(uv.y * p_frequency * 6.2832 + ph) * p_amplitude;
    } else {
        p.y += sin(uv.x * p_frequency * 6.2832 + ph) * p_amplitude;
    }
    if (p.x < -0.0001 || p.x > 1.0001 || p.y < -0.0001 || p.y > 1.0001) return vec4(0.0);
    return texture2D(u_input, clamp(p, 0.0, 1.0));
}
