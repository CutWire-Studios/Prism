vec4 effect(vec2 uv) {
    float cell = max(p_size, 1.0);
    vec2 s = vec2(1.0, 1.7320508);
    vec2 p = uv * u_resolution / cell;
    vec2 a = mod(p, s) - s * 0.5;
    vec2 b = mod(p - s * 0.5, s) - s * 0.5;
    vec2 c = p - (dot(a, a) < dot(b, b) ? a : b);
    return texture2D(u_input, clamp(c * cell / u_resolution, 0.0, 1.0));
}
