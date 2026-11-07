vec4 effect(vec2 uv) {
    float t = u_time;
    float wob = (valueNoise(vec2(uv.y * 8.0, t * 4.0)) - 0.5) * 0.04 * p_wobble;
    float band = smoothstep(0.96, 1.0, sin(uv.y * 3.0 - t * 1.7) * 0.5 + 0.5);
    vec2 p = vec2(uv.x + wob + band * 0.02 * p_wobble, uv.y);
    vec4 g = texture2D(u_input, clamp(p, 0.0, 1.0));
    float bleed = 0.01 * p_bleed;
    float r = texture2D(u_input, clamp(p + vec2(bleed, 0.0), 0.0, 1.0)).r;
    float b = texture2D(u_input, clamp(p - vec2(bleed, 0.0), 0.0, 1.0)).b;
    float n = hash21(floor(uv * u_resolution * 0.5) + floor(t * 30.0)) - 0.5;
    vec3 col = vec3(r, g.g, b) + n * 0.08 * p_noise + band * 0.15 * p_noise;
    return vec4(clamp(col, 0.0, 1.0), g.a);
}
