vec3 brightPart(vec3 c) {
    return max(c - vec3(p_threshold), vec3(0.0)) / max(1.0 - p_threshold, 0.0001);
}

vec4 effect(vec2 uv) {
    vec4 col = texture2D(u_input, uv);
    vec3 glow = vec3(0.0);
    for (int ring = 0; ring < 2; ++ring) {
        float dist = p_radius * float(ring + 1) / 2.0;
        for (int k = 0; k < 12; ++k) {
            float a = (float(k) + 0.5 * float(ring)) * 6.2831853 / 12.0;
            vec2 o = vec2(cos(a), sin(a)) * dist / u_resolution;
            glow += brightPart(texture2D(u_input, clamp(uv + o, 0.0, 1.0)).rgb);
        }
    }
    glow /= 24.0;
    return vec4(clamp(col.rgb + glow * p_intensity, 0.0, 1.0), col.a);
}
