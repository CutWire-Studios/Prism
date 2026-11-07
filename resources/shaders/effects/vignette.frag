vec4 effect(vec2 uv) {
    vec4 col = texture2D(u_input, uv);
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 d = (uv - 0.5) * 2.0 * vec2(asp, 1.0) / min(asp, 1.0);
    float r = mix(max(abs(d.x), abs(d.y)), length(d), p_roundness);
    float v = smoothstep(1.0 - p_softness, 1.0, r);
    return vec4(col.rgb * (1.0 - p_amount * v), col.a);
}
