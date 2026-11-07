float lumAt(vec2 p) {
    return dot(texture2D(u_input, clamp(p, 0.0, 1.0)).rgb, vec3(0.2126, 0.7152, 0.0722));
}

vec4 effect(vec2 uv) {
    vec4 col = texture2D(u_input, uv);
    vec2 px = 1.0 / u_resolution;
    float gx = -lumAt(uv + vec2(-px.x, -px.y)) - 2.0 * lumAt(uv + vec2(-px.x, 0.0)) - lumAt(uv + vec2(-px.x, px.y))
             + lumAt(uv + vec2(px.x, -px.y)) + 2.0 * lumAt(uv + vec2(px.x, 0.0)) + lumAt(uv + vec2(px.x, px.y));
    float gy = -lumAt(uv + vec2(-px.x, -px.y)) - 2.0 * lumAt(uv + vec2(0.0, -px.y)) - lumAt(uv + vec2(px.x, -px.y))
             + lumAt(uv + vec2(-px.x, px.y)) + 2.0 * lumAt(uv + vec2(0.0, px.y)) + lumAt(uv + vec2(px.x, px.y));
    float e = clamp(length(vec2(gx, gy)) * p_strength, 0.0, 1.0);
    if (p_mode == 1) return vec4(vec3(e), col.a);
    return vec4(col.rgb * (1.0 - e), col.a);
}
