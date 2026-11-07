vec4 effect(vec2 uv) {
    vec4 col = texture2D(u_input, uv);
    float l = dot(col.rgb, vec3(0.2126, 0.7152, 0.0722));
    float m = smoothstep(p_threshold - p_softness, p_threshold + p_softness + 0.0001, l);
    return vec4(vec3(m), col.a);
}
