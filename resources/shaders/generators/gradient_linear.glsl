vec4 generate(vec2 uv) {
    float a = radians(p_angle);
    vec2 d = vec2(cos(a), sin(a));
    float t = dot(uv - 0.5, d) / (abs(d.x) + abs(d.y)) + 0.5;
    t = triangleWave(t + u_time * p_speed);
    return vec4(ramp3(p_color_a.rgb, p_color_b.rgb, p_color_mid.rgb, p_use_mid, t), 1.0);
}
