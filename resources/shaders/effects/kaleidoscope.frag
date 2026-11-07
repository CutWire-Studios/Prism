vec4 effect(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 d = (uv - p_center) * vec2(asp, 1.0);
    float seg = 6.2832 / max(float(p_segments), 2.0);
    float a = atan(d.y, d.x) + radians(p_rotation);
    a = mod(a, seg);
    a = abs(a - seg * 0.5);
    vec2 nd = vec2(cos(a), sin(a)) * length(d);
    vec2 q = p_center + vec2(nd.x / asp, nd.y);
    return texture2D(u_input, 1.0 - abs(mod(q, 2.0) - 1.0));
}
