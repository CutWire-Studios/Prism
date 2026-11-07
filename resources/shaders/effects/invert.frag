vec4 effect(vec2 uv) {
    vec4 c = texture2D(u_input, uv);
    return vec4(mix(c.rgb, 1.0 - c.rgb, p_amount), c.a);
}
