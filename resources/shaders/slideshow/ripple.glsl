// Radial water-ripple displacement
const float PI = 3.141592653589793;

vec4 transition(vec2 uv) {
    float p = u_progress;
    vec2 center = vec2(0.5);
    vec2 diff = uv - center;
    diff.x *= u_ratio;
    float dist = length(diff);

    float wave = sin(dist * 20.0 - p * 8.0 * PI) * (1.0 - p) * 0.03;
    vec2 offset = normalize(diff + 0.0001) * wave;
    offset.x /= u_ratio;

    vec4 fromCol = getFromColor(uv + offset);
    vec4 toCol   = getToColor(uv - offset);
    return mix(fromCol, toCol, smoothstep(0.0, 1.0, p));
}
