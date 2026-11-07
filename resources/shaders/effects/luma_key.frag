vec4 effect(vec2 uv) {
    vec4 col = texture2D(u_input, uv);
    float l = dot(col.rgb, vec3(0.2126, 0.7152, 0.0722));
    float s = max(p_softness, 0.0001);
    float k = smoothstep(p_threshold - s, p_threshold + s, l);
    if (p_invert) k = 1.0 - k;
    return vec4(col.rgb, col.a * k);
}
