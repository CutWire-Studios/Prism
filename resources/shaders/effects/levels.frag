vec4 effect(vec2 uv) {
    vec4 col = texture2D(u_input, uv);
    float range = max(p_in_white - p_in_black, 0.0001);
    vec3 rgb = clamp((col.rgb - p_in_black) / range, 0.0, 1.0);
    rgb = pow(rgb, vec3(1.0 / max(p_gamma, 0.0001)));
    return vec4(mix(vec3(p_out_black), vec3(p_out_white), rgb), col.a);
}
