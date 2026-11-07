vec4 effect(vec2 uv) {
    if (p_index == 1) return texture2D(u_input1, uv);
    if (p_index == 2) return texture2D(u_input2, uv);
    if (p_index == 3) return texture2D(u_input3, uv);
    return texture2D(u_input, uv);
}
