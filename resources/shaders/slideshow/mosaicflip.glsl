// Grid of tiles flipping in 3D
const float PI = 3.141592653589793;
const float GRID = 8.0;

vec4 transition(vec2 uv) {
    vec2 cell = floor(uv * GRID);
    vec2 cellUv = fract(uv * GRID);

    float delay = (cell.x + cell.y) / (GRID * 2.0);
    float localP = clamp((u_progress - delay * 0.6) / 0.4, 0.0, 1.0);
    float angle = localP * PI;
    float c = cos(angle);

    vec2 centered = (cellUv - 0.5) * 2.0;
    float z = c + 0.001;
    vec2 proj = vec2(centered.x / z, centered.y);
    vec2 sampleUv = proj * 0.5 + 0.5;

    vec2 globalUv = (cell + sampleUv) / GRID;

    if (c > 0.0)
        return getFromColor(globalUv);
    globalUv.x = (cell.x + (1.0 - sampleUv.x)) / GRID;
    return getToColor(globalUv);
}
