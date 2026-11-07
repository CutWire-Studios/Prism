vec4 effect(vec2 uv) {
    vec4 src = texture2D(u_input, uv);
    vec4 r = vec4(src.rgb * src.a, src.a);
    float w = 1.0;
    for (int i = 1; i <= 8; ++i) {
        if (i > p_echoes) break;
        w *= p_decay;
        vec4 e = historyFrame(float(i * p_spacing), uv);
        r = max(r, vec4(e.rgb * e.a, e.a) * w);
    }
    return vec4(r.rgb / max(r.a, 0.0001), r.a);
}
