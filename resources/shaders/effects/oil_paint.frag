vec4 effect(vec2 uv) {
    float R = p_quality == 0 ? 2.0 : 3.0;
    float scale = p_radius / R;
    vec3 m0 = vec3(0.0); vec3 m1 = vec3(0.0); vec3 m2 = vec3(0.0); vec3 m3 = vec3(0.0);
    vec3 q0 = vec3(0.0); vec3 q1 = vec3(0.0); vec3 q2 = vec3(0.0); vec3 q3 = vec3(0.0);
    float n0 = 0.0; float n1 = 0.0; float n2 = 0.0; float n3 = 0.0;
    for (int j = -3; j <= 3; ++j) {
        for (int i = -3; i <= 3; ++i) {
            float fi = float(i);
            float fj = float(j);
            if (abs(fi) > R || abs(fj) > R) continue;
            vec3 s = texture2D(u_input, clamp(uv + vec2(fi, fj) * scale / u_resolution, 0.0, 1.0)).rgb;
            if (i <= 0 && j <= 0) { m0 += s; q0 += s * s; n0 += 1.0; }
            if (i >= 0 && j <= 0) { m1 += s; q1 += s * s; n1 += 1.0; }
            if (i <= 0 && j >= 0) { m2 += s; q2 += s * s; n2 += 1.0; }
            if (i >= 0 && j >= 0) { m3 += s; q3 += s * s; n3 += 1.0; }
        }
    }
    m0 /= n0; m1 /= n1; m2 /= n2; m3 /= n3;
    q0 = q0 / n0 - m0 * m0;
    q1 = q1 / n1 - m1 * m1;
    q2 = q2 / n2 - m2 * m2;
    q3 = q3 / n3 - m3 * m3;
    float v0 = q0.r + q0.g + q0.b;
    float v1 = q1.r + q1.g + q1.b;
    float v2 = q2.r + q2.g + q2.b;
    float v3 = q3.r + q3.g + q3.b;
    vec3 best = m0;
    float bv = v0;
    if (v1 < bv) { best = m1; bv = v1; }
    if (v2 < bv) { best = m2; bv = v2; }
    if (v3 < bv) { best = m3; }
    return vec4(best, texture2D(u_input, uv).a);
}
