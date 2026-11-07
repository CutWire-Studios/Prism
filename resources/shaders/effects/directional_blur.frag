vec4 effect(vec2 uv) {
    float a = radians(p_angle);
    vec2 dir = vec2(cos(a), sin(a)) * p_length / u_resolution;
    vec4 sum = vec4(0.0);
    for (int k = 0; k < 24; ++k) {
        float t = float(k) / 23.0 - 0.5;
        sum += texture2D(u_input, clamp(uv + dir * t, 0.0, 1.0));
    }
    return sum / 24.0;
}
