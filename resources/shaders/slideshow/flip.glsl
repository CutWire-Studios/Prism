// 3D card flip on Y axis
const float PI = 3.141592653589793;

vec4 transition(vec2 uv) {
    float angle = u_progress * PI;
    float c = cos(angle);
    float s = sin(angle);

    vec2 centered = (uv - 0.5) * 2.0;
    centered.x *= u_ratio;

    float z = c + 0.001;
    vec2 proj = vec2(centered.x / z, centered.y);
    proj.x /= u_ratio;
    vec2 sampleUv = proj * 0.5 + 0.5;

    if (c > 0.0) {
        if (sampleUv.x >= 0.0 && sampleUv.x <= 1.0 &&
            sampleUv.y >= 0.0 && sampleUv.y <= 1.0)
            return getFromColor(sampleUv);
    } else {
        sampleUv.x = 1.0 - sampleUv.x;
        if (sampleUv.x >= 0.0 && sampleUv.x <= 1.0 &&
            sampleUv.y >= 0.0 && sampleUv.y <= 1.0)
            return getToColor(sampleUv);
    }
    return vec4(0.0, 0.0, 0.0, 1.0);
}
