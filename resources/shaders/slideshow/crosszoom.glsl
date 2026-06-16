// Directional zoom-blur cross dissolve
vec4 transition(vec2 uv) {
    float p = u_progress;
    vec2 dir = vec2(0.0, 1.0);
    float strength = 0.04;

    vec4 fromCol = vec4(0.0);
    vec4 toCol = vec4(0.0);
    float total = 0.0;

    for (int i = -4; i <= 4; ++i) {
        float fi = float(i);
        float w = 1.0 - abs(fi) / 5.0;
        vec2 offset = dir * fi * strength * p;
        fromCol += getFromColor(uv + offset * (1.0 - p)) * w;
        toCol   += getToColor(uv - offset * p) * w;
        total += w;
    }
    fromCol /= total;
    toCol   /= total;

    return mix(fromCol, toCol, smoothstep(0.0, 1.0, p));
}
