vec4 generate(vec2 uv) {
    vec2 p = centered(uv) * p_count + u_time * p_speed;
    if (p_stagger)
        p.x += 0.5 * mod(floor(p.y), 2.0);
    vec2 cell = floor(p);
    float r = p_radius * (1.0 + p_pulse * sin(u_time * 2.0 + hash21(cell) * 6.2831853));
    float d = length(fract(p) - 0.5);
    float m = 1.0 - smoothstep(r, r + max(p_softness, 0.001), d);
    return vec4(mix(p_color_bg.rgb, p_color_dot.rgb, m), 1.0);
}
