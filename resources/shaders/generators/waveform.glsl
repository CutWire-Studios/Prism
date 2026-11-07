// There is no raw waveform uniform, so the trace is a sum of 16 sine harmonics weighted by the spectrum.
float waveAt(float x) {
    float w = 0.0;
    for (int k = 0; k < 16; ++k) {
        float fk = float(k);
        float s = spectrumAt((fk + 0.5) / 16.0);
        w += s * sin(3.1415927 * (fk + 1.0) * x + u_time * (1.0 + fk * 0.35) + fk * 1.7);
    }
    return w * 0.22 * sin(3.1415927 * x);
}

vec4 generate(vec2 uv) {
    float y = 0.5 + waveAt(uv.x) * p_amplitude;
    float d = abs(uv.y - y);
    float line = 1.0 - smoothstep(p_thickness * 0.5, p_thickness * 0.5 + 0.004, d);
    float glow = p_glow * 0.35 * 0.02 / (d + 0.02);
    vec3 col = p_color_bg.rgb + p_color_line.rgb * (line + glow);
    return vec4(clamp(col, 0.0, 1.0), 1.0);
}
