// Rotational vortex warp
const float PI = 3.141592653589793;

vec4 transition(vec2 uv) {
    float p = u_progress;
    vec2 center = vec2(0.5);
    vec2 diff = uv - center;
    diff.x *= u_ratio;

    float dist = length(diff);
    float angle = atan(diff.y, diff.x);
    float twist = (1.0 - p) * dist * 6.0 * PI;

    vec2 fromDiff = vec2(cos(angle + twist), sin(angle + twist)) * dist;
    fromDiff.x /= u_ratio;
    vec2 fromUv = fromDiff + center;

    vec2 toDiff = vec2(cos(angle - twist * p), sin(angle - twist * p)) * dist;
    toDiff.x /= u_ratio;
    vec2 toUv = toDiff + center;

    return mix(getFromColor(fromUv), getToColor(toUv), smoothstep(0.0, 1.0, p));
}
