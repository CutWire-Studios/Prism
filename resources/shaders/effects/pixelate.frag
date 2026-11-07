vec4 effect(vec2 uv) {
    vec2 cell = max(p_size, 1.0) / u_resolution;
    vec2 p = (floor(uv / cell) + 0.5) * cell;
    return texture2D(u_input, clamp(p, 0.0, 1.0));
}
