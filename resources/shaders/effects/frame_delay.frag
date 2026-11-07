vec4 effect(vec2 uv) {
    if (p_delay <= 0) return texture2D(u_input, uv);
    return historyFrame(float(p_delay), uv);
}
