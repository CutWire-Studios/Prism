vec4 effect(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 d = (uv - p_center) * vec2(asp, 1.0);
    vec4 sum = vec4(0.0);
    for (int k = 0; k < 24; ++k) {
        float a = radians(p_angle) * (float(k) / 23.0 - 0.5);
        vec2 q = rot2(a) * d;
        sum += texture2D(u_input, clamp(p_center + vec2(q.x / asp, q.y), 0.0, 1.0));
    }
    return sum / 24.0;
}
