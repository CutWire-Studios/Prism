vec4 effect(vec2 uv) {
    vec4 col = texture2D(u_input, uv);
    return vec4(clamp(col.rgb * pow(2.0, p_stops), 0.0, 1.0), col.a);
}
