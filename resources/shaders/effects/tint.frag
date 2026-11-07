vec4 effect(vec2 uv) {
    vec4 col = texture2D(u_input, uv);
    float l = dot(col.rgb, vec3(0.2126, 0.7152, 0.0722));
    vec3 tinted = l * p_color.rgb * 1.6;
    return vec4(mix(col.rgb, clamp(tinted, 0.0, 1.0), p_amount), col.a);
}
