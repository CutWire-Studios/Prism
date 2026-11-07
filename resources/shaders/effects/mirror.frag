vec4 effect(vec2 uv) {
    vec2 p = uv;
    if (p_mode == 0) {
        p = vec2(0.5 - abs(uv.x - 0.5), uv.y);
    } else if (p_mode == 1) {
        p = vec2(uv.x, 0.5 - abs(uv.y - 0.5));
    } else if (p_mode == 2) {
        p = 0.5 - abs(uv - 0.5);
    } else {
        p = 1.0 - abs(mod(uv * 2.0, 2.0) - 1.0);
    }
    return texture2D(u_input, p);
}
