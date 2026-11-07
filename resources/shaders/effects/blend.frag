vec4 effect(vec2 uv) {
    vec4 base = texture2D(u_input, uv);
    vec4 top = texture2D(u_input1, uv);

    // Choice order: add, screen, multiply, overlay, difference, soft light, lighten, darken.
    int mode = 5;
    if (p_mode == 1) mode = 2;
    else if (p_mode == 2) mode = 1;
    else if (p_mode == 3) mode = 3;
    else if (p_mode == 4) mode = 6;
    else if (p_mode == 5) mode = 4;
    else if (p_mode == 6) mode = 7;
    else if (p_mode == 7) mode = 8;

    float w = clamp(p_opacity, 0.0, 1.0) * top.a;
    // Where the base is see-through the layer shows as itself, not blended with nothing.
    vec3 blended = mix(top.rgb, blend(base.rgb, top.rgb, mode), base.a);
    float outA = w + base.a * (1.0 - w);
    vec3 rgb = (blended * w + base.rgb * base.a * (1.0 - w)) / max(outA, 0.00001);
    return vec4(rgb, outA);
}
