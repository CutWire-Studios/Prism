vec4 effect(vec2 uv) {
    vec4 col = texture2D(u_input, uv);
    float l = dot(col.rgb, vec3(0.2126, 0.7152, 0.0722));
    vec3 duo = mix(p_shadow.rgb, p_highlight.rgb, smoothstep(0.0, 1.0, l));
    return vec4(duo, col.a);
}
