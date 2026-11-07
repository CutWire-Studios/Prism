vec4 effect(vec2 uv) {
    vec4 col = texture2D(u_input, uv);
    float l = dot(col.rgb, vec3(0.2126, 0.7152, 0.0722));
    float chroma = max(max(col.r, col.g), col.b) - min(min(col.r, col.g), col.b);
    vec3 rgb = mix(vec3(l), col.rgb, p_saturation);
    rgb = mix(vec3(l), rgb, 1.0 + p_vibrance * (1.0 - chroma));
    return vec4(clamp(rgb, 0.0, 1.0), col.a);
}
