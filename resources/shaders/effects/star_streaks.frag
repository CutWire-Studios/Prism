vec4 effect(vec2 uv) {
    vec4 col = texture2D(u_input, uv);
    float lines = 2.0 + float(p_spikes);
    vec3 sum = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        if (float(i) >= lines) break;
        float a = float(i) * 3.14159265 / lines;
        vec2 dir = vec2(cos(a), sin(a)) * p_length / u_resolution;
        for (int k = 1; k <= 6; ++k) {
            float t = float(k) / 7.0;
            float w = (1.0 - t) * (1.0 - t);
            vec3 s1 = texture2D(u_input, clamp(uv + dir * t, 0.0, 1.0)).rgb;
            vec3 s2 = texture2D(u_input, clamp(uv - dir * t, 0.0, 1.0)).rgb;
            sum += (max(s1 - p_threshold, 0.0) + max(s2 - p_threshold, 0.0)) * w;
        }
    }
    // 1.857 is the summed tap weight of one ray, so one spike has the same strength whatever the spike count.
    sum /= 1.857 * max(1.0 - p_threshold, 0.0001);
    return vec4(clamp(col.rgb + sum * p_intensity, 0.0, 1.0), col.a);
}
