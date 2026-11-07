float starLayer(vec2 p) {
    vec2 gv = fract(p) - 0.5;
    vec2 id = floor(p);
    float acc = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 offs = vec2(float(x), float(y));
            float n = hash21(id + offs);
            float size = fract(n * 345.32);
            vec2 o = hash22(id + offs + 7.0) - 0.5;
            float d = length(gv - offs - o * 0.8);
            float g = 0.035 * (0.35 + size) / (d + 0.01);
            g *= smoothstep(0.5, 0.1, d);
            float tw = 0.65 + 0.35 * sin(u_time * 3.0 + n * 62.83);
            acc += g * mix(1.0, tw, p_twinkle);
        }
    }
    return acc;
}

vec4 generate(vec2 uv) {
    vec2 p = centered(uv);
    float acc = 0.0;
    for (int i = 0; i < 6; ++i) {
        if (i >= p_layers)
            break;
        float depth = fract(float(i) / float(p_layers) + u_time * p_speed);
        float scale = mix(18.0, 0.8, depth);
        float fade = depth * smoothstep(1.0, 0.85, depth);
        acc += starLayer(p * scale + float(i) * 453.2) * fade;
    }
    vec3 col = p_color_bg.rgb + p_color_star.rgb * acc * p_brightness;
    return vec4(clamp(col, 0.0, 1.0), 1.0);
}
