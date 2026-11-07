vec4 effect(vec2 uv) {
    vec4 m = texture2D(u_input1, uv);
    vec2 d;
    if (p_channels == 0) d = m.rg - 0.5;
    else d = vec2(dot(m.rgb, vec3(0.2126, 0.7152, 0.0722)) - 0.5);
    // A see-through map pixel displaces nothing, so an unconnected map is a no-op.
    d *= m.a;
    return texture2D(u_input, uv + d * p_amount);
}
