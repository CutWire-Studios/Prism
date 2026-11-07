vec4 generate(vec2 uv) {
    float a = radians(p_angle);
    vec2 p = centered(uv);
    float s = fract(dot(p, vec2(cos(a), sin(a))) * p_count + u_time * p_speed);
    float soft = max(p_softness, 0.001);
    float m = smoothstep(0.0, soft, s) * (1.0 - smoothstep(p_duty, p_duty + soft, s));
    return vec4(mix(p_color_a.rgb, p_color_b.rgb, m), 1.0);
}
