vec4 effect(vec2 uv) {
    vec4 src = texture2D(u_input, uv);
    vec4 prev = texture2D(u_prev, uv);
    vec4 sp = vec4(src.rgb * src.a, src.a);
    vec4 pp = vec4(prev.rgb * prev.a, prev.a);
    vec4 r;
    if (p_blend == 1) r = min(sp + pp * p_decay, 1.0);
    else if (p_blend == 2) r = vec4(mix(sp.rgb, pp.rgb, p_decay * pp.a), max(sp.a, pp.a * p_decay));
    else r = max(sp, pp * p_decay);
    return vec4(r.rgb / max(r.a, 0.0001), r.a);
}
