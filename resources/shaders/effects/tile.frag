vec4 effect(vec2 uv) {
    vec2 count = max(vec2(p_count_x, p_count_y), vec2(0.0001));
    vec2 p = uv * count;
    if (p_mode == 1) {
        p = 1.0 - abs(mod(p, 2.0) - 1.0);
    } else {
        p = p - min(floor(p), ceil(count) - 1.0);
    }
    return texture2D(u_input, p);
}
