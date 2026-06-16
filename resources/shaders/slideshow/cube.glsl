// Perspective rotating cube transition
const float PI = 3.141592653589793;

vec4 transition(vec2 uv) {
    float p = u_progress;
    float angle = p * PI * 0.5;
    float c = cos(angle);
    float s = sin(angle);

    vec2 centered = (uv - 0.5) * 2.0;
    centered.x *= u_ratio;

    float z = 1.0;
    if (p < 0.5) {
        float rot = s / (c + 0.001);
        z = 1.0 + abs(rot) * 0.5;
        vec2 proj = vec2(centered.x / z, centered.y / z);
        proj.x /= u_ratio;
        vec2 sampleUv = proj * 0.5 + 0.5;
        if (sampleUv.x >= 0.0 && sampleUv.x <= 1.0 &&
            sampleUv.y >= 0.0 && sampleUv.y <= 1.0)
            return getFromColor(sampleUv);
        return vec4(0.0, 0.0, 0.0, 1.0);
    } else {
        float rot = sin(angle) / (cos(angle) + 0.001);
        z = 1.0 + abs(rot) * 0.5;
        vec2 proj = vec2(centered.x / z, centered.y / z);
        proj.x /= u_ratio;
        vec2 sampleUv = proj * 0.5 + 0.5;
        if (sampleUv.x >= 0.0 && sampleUv.x <= 1.0 &&
            sampleUv.y >= 0.0 && sampleUv.y <= 1.0)
            return getToColor(sampleUv);
        return vec4(0.0, 0.0, 0.0, 1.0);
    }
}
