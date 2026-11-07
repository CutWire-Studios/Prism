vec4 generate(vec2 uv) {
    float aspect = u_resolution.x / u_resolution.y;
    vec2 d = (uv - p_center) * vec2(aspect, 1.0);
    float turns = atan(d.y, d.x) / 6.2831853 + radians(p_angle) / 6.2831853 + u_time * p_speed;
    float t = 1.0 - abs(1.0 - 2.0 * fract(turns * float(p_repeat)));
    return vec4(ramp3(p_color_a.rgb, p_color_b.rgb, p_color_mid.rgb, p_use_mid, t), 1.0);
}
