vec4 effect(vec2 uv) {
    vec4 col = texture2D(u_input, uv);
    float n = max(float(p_levels), 2.0) - 1.0;
    return vec4(floor(col.rgb * n + 0.5) / n, col.a);
}
