vec4 effect(vec2 uv) {
    vec4 col = texture2D(u_input, uv);
    vec2 cell = floor(uv * u_resolution / p_size);
    float n = hash21(cell + floor(u_time * 24.0) * 17.31) - 0.5;
    return vec4(clamp(col.rgb + n * p_amount, 0.0, 1.0), col.a);
}
