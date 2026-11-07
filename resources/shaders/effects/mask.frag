vec4 effect(vec2 uv) {
    vec4 col = texture2D(u_input, uv);
    vec4 m = texture2D(u_input1, uv);
    float v = p_mode == 0 ? dot(m.rgb, vec3(0.2126, 0.7152, 0.0722)) * m.a : m.a;
    if (p_invert) v = 1.0 - v;
    return vec4(col.rgb, col.a * v);
}
