vec4 effect(vec2 uv) {
    vec4 col = texture2D(u_input, uv);
    vec3 x = clamp(col.rgb, 0.0, 1.0);
    x = clamp(x + vec3(p_lift_r, p_lift_g, p_lift_b) * (1.0 - x), 0.0, 1.0);
    x = clamp(x * vec3(p_gain_r, p_gain_g, p_gain_b), 0.0, 1.0);
    vec3 gam = max(vec3(p_gamma_r, p_gamma_g, p_gamma_b), vec3(0.0001));
    x = pow(x, 1.0 / gam);
    return vec4(x, col.a);
}
