vec4 effect(vec2 uv) {
    vec4 sum = vec4(0.0);
    for (int k = 0; k < 24; ++k) {
        float s = 1.0 - p_strength * float(k) / 23.0;
        sum += texture2D(u_input, clamp(p_center + (uv - p_center) * s, 0.0, 1.0));
    }
    return sum / 24.0;
}
