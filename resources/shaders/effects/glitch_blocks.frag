vec4 effect(vec2 uv) {
    float tick = floor(u_time * p_speed);
    vec2 cell = floor(uv * u_resolution / p_size);
    float h = hash21(cell + tick * 17.31);
    float on = step(1.0 - p_amount * 0.5, h);
    float shift = (hash21(cell * 1.7 + tick * 3.1) - 0.5) * 0.3 * p_amount * on;
    float bleed = 0.01 * p_amount * on;
    vec2 p = uv + vec2(shift, 0.0);
    float r = texture2D(u_input, clamp(p + vec2(bleed, 0.0), 0.0, 1.0)).r;
    vec4 g = texture2D(u_input, clamp(p, 0.0, 1.0));
    float b = texture2D(u_input, clamp(p - vec2(bleed, 0.0), 0.0, 1.0)).b;
    return vec4(r, g.g, b, g.a);
}
