// Outgoing recedes into doorway, incoming flies forward
vec4 transition(vec2 uv) {
    float p = u_progress;
    vec2 centered = (uv - 0.5) * 2.0;
    centered.x *= u_ratio;

    if (p < 0.5) {
        float scale = 1.0 - p * 1.6;
        vec2 proj = centered / max(scale, 0.01);
        proj.x /= u_ratio;
        vec2 sampleUv = proj * 0.5 + 0.5;
        if (sampleUv.x >= 0.0 && sampleUv.x <= 1.0 &&
            sampleUv.y >= 0.0 && sampleUv.y <= 1.0)
            return getFromColor(sampleUv);
        return vec4(0.0, 0.0, 0.0, 1.0);
    } else {
        float t = (p - 0.5) * 2.0;
        float scale = 0.4 + t * 0.6;
        vec2 proj = centered / scale;
        proj.x /= u_ratio;
        vec2 sampleUv = proj * 0.5 + 0.5;
        if (sampleUv.x >= 0.0 && sampleUv.x <= 1.0 &&
            sampleUv.y >= 0.0 && sampleUv.y <= 1.0)
            return getToColor(sampleUv);
        return vec4(0.0, 0.0, 0.0, 1.0);
    }
}
