vec4 effect(vec2 uv) {
    vec4 col = texture2D(u_input, uv);
    float t = clamp(dot(col.rgb, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0);
    vec3 g = t < 0.5
        ? mix(p_shadows.rgb, p_mids.rgb, t * 2.0)
        : mix(p_mids.rgb, p_highlights.rgb, (t - 0.5) * 2.0);
    return vec4(g, col.a);
}
