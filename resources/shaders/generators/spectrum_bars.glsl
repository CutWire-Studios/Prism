vec4 generate(vec2 uv) {
    float n = float(p_bars);
    float x = uv.x * n;
    float id = floor(x);
    float fx = fract(x);
    float v = clamp(spectrumAt((id + 0.5) / n) * p_gain, 0.0, 1.0);
    float h = p_mirror ? abs(uv.y - 0.5) * 2.0 : uv.y;
    float inBar = step(p_gap * 0.5, fx) * step(fx, 1.0 - p_gap * 0.5);
    float filled = 1.0 - smoothstep(v - 0.004, v, h);
    vec3 col = mix(p_color_low.rgb, p_color_high.rgb, clamp(h, 0.0, 1.0));
    return vec4(mix(p_color_bg.rgb, col, inBar * filled), 1.0);
}
