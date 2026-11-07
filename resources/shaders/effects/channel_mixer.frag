vec4 effect(vec2 uv) {
    vec4 col = texture2D(u_input, uv);
    vec3 rgb = vec3(
        dot(col.rgb, vec3(p_rr, p_rg, p_rb)),
        dot(col.rgb, vec3(p_gr, p_gg, p_gb)),
        dot(col.rgb, vec3(p_br, p_bg, p_bb)));
    return vec4(clamp(rgb, 0.0, 1.0), col.a);
}
