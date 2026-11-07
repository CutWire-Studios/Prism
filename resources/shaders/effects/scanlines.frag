vec4 effect(vec2 uv) {
    vec4 col = texture2D(u_input, uv);
    float l = 0.5 + 0.5 * sin((uv.y + u_time * p_speed * 0.05) * p_density * 3.14159265);
    return vec4(col.rgb * (1.0 - p_intensity * (1.0 - l)), col.a);
}
