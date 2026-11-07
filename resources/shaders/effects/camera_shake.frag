vec4 effect(vec2 uv) {
    float ts = u_time * p_speed;
    vec2 j = vec2(valueNoise(vec2(ts, 3.7)), valueNoise(vec2(9.1, ts))) - 0.5;
    vec2 p = uv + j * 2.0 * p_amount;
    if (p.x < -0.0001 || p.x > 1.0001 || p.y < -0.0001 || p.y > 1.0001) return vec4(0.0);
    return texture2D(u_input, clamp(p, 0.0, 1.0));
}
