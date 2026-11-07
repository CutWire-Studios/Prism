vec4 generate(vec2 uv) {
    vec2 p = centered(uv) * p_count + u_time * p_speed;
    vec2 f = fract(p);
    float e = min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y));
    float line = 1.0 - smoothstep(p_width, p_width + max(p_softness, 0.001), e);
    return vec4(mix(p_color_bg.rgb, p_color_line.rgb, line), 1.0);
}
