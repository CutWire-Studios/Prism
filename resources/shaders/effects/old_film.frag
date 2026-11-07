vec4 effect(vec2 uv) {
    vec4 col = texture2D(u_input, uv);
    float frame = floor(u_time * 18.0);
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec3 rgb = col.rgb * (1.0 + (hash11(frame + 9.1) - 0.5) * 0.3 * p_flicker);
    vec3 sep = vec3(
        dot(rgb, vec3(0.393, 0.769, 0.189)),
        dot(rgb, vec3(0.349, 0.686, 0.168)),
        dot(rgb, vec3(0.272, 0.534, 0.131)));
    rgb = mix(rgb, clamp(sep, 0.0, 1.0), p_sepia);
    for (int i = 0; i < 3; ++i) {
        float fi = float(i);
        float on = step(1.0 - p_scratches * 0.6, hash11(frame * 1.7 + fi * 11.0));
        float x = hash11(frame + fi * 5.7);
        float line = 1.0 - smoothstep(0.0, 0.0015, abs(uv.x - x - sin(uv.y * 9.0 + fi) * 0.003));
        rgb = mix(rgb, vec3(0.9), line * on * 0.7);
    }
    float vd = length((uv - 0.5) * vec2(asp, 1.0));
    rgb *= 1.0 - p_vignette * smoothstep(0.35, 1.0, vd);
    return vec4(clamp(rgb, 0.0, 1.0), col.a);
}
