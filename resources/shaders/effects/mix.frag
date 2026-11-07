vec4 effect(vec2 uv) {
    vec4 a = texture2D(u_input, uv);
    vec4 b = texture2D(u_input1, uv);
    float t = clamp(p_amount, 0.0, 1.0);
    vec3 premul = mix(a.rgb * a.a, b.rgb * b.a, t);
    float alpha = mix(a.a, b.a, t);
    return vec4(premul / max(alpha, 0.00001), alpha);
}
