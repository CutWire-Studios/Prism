// RGB-split / block-displacement glitch
float rand(vec2 co) {
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

vec4 transition(vec2 uv) {
    float p = u_progress;
    float intensity = sin(p * 3.14159) * 0.08;

    vec2 block = floor(uv * 20.0) / 20.0;
    float r = rand(block + floor(p * 30.0));
    vec2 offset = vec2(r - 0.5, rand(block + 1.0) - 0.5) * intensity;

    vec4 fromCol;
    fromCol.r = getFromColor(uv + offset).r;
    fromCol.g = getFromColor(uv).g;
    fromCol.b = getFromColor(uv - offset).b;
    fromCol.a = 1.0;

    vec4 toCol;
    toCol.r = getToColor(uv + offset).r;
    toCol.g = getToColor(uv).g;
    toCol.b = getToColor(uv - offset).b;
    toCol.a = 1.0;

    float split = step(0.5, p);
    return mix(fromCol, toCol, split);
}
