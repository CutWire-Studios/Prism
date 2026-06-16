// Page peel with back-face shading
vec4 transition(vec2 uv) {
    float p = u_progress;
    float curl = p * 1.2;

    vec2 o = uv - 0.5;
    o.x *= u_ratio;

    float dist = o.x + o.y * 0.3;
    float fold = smoothstep(curl - 0.15, curl, dist);

    if (fold < 0.5) {
        vec2 fromUv = uv;
        fromUv.x += fold * 0.05;
        return getFromColor(fromUv);
    }

    vec2 toUv = uv;
    toUv.x -= (1.0 - fold) * 0.05;
    vec4 col = getToColor(toUv);

    float shade = 0.6 + 0.4 * (1.0 - abs(fold - 0.5) * 2.0);
    col.rgb *= shade;
    return col;
}
