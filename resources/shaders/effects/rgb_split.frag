vec4 effect(vec2 uv) {
    float a = radians(p_angle);
    vec2 delta = vec2(cos(a), sin(a)) * p_amount / u_resolution;
    vec4 g = texture2D(u_input, uv);
    float r = texture2D(u_input, clamp(uv + delta, 0.0, 1.0)).r;
    float b = texture2D(u_input, clamp(uv - delta, 0.0, 1.0)).b;
    return vec4(r, g.g, b, g.a);
}
