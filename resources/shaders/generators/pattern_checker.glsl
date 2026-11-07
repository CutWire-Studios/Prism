vec4 generate(vec2 uv) {
    vec2 p = rot2(radians(p_angle)) * centered(uv) * p_count;
    p += u_time * p_speed * vec2(1.0, 0.5);
    float c = mod(floor(p.x) + floor(p.y), 2.0);
    return vec4(mix(p_color_a.rgb, p_color_b.rgb, c), 1.0);
}
