vec4 generate(vec2 uv) {
    float aspect = u_resolution.x / u_resolution.y;
    float r = p_radius * (1.0 + p_pulse * sin(u_time * p_speed * 6.2831853));
    float t = length((uv - p_center) * vec2(aspect, 1.0)) / max(r, 0.001);
    return vec4(ramp3(p_color_a.rgb, p_color_b.rgb, p_color_mid.rgb, p_use_mid, t), 1.0);
}
