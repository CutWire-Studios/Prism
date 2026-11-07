vec4 effect(vec2 uv) {
    vec4 col = texture2D(u_input, uv);
    vec3 rgb = (col.rgb - 0.5) * p_contrast + 0.5 + p_brightness;
    return vec4(clamp(rgb, 0.0, 1.0), col.a);
}
