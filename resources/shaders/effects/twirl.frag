vec4 effect(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 d = (uv - p_center) * vec2(asp, 1.0);
    float r = length(d);
    float fall = 1.0 - smoothstep(0.0, max(p_radius, 0.0001), r);
    float a = atan(d.y, d.x) + radians(p_angle) * fall * fall;
    vec2 nd = r > 0.000001 ? vec2(cos(a), sin(a)) * r : d;
    vec2 p = p_center + vec2(nd.x / asp, nd.y);
    if (p.x < -0.0001 || p.x > 1.0001 || p.y < -0.0001 || p.y > 1.0001) return vec4(0.0);
    return texture2D(u_input, clamp(p, 0.0, 1.0));
}
