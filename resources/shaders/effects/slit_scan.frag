vec4 effect(vec2 uv) {
    float t = p_direction == 0 ? uv.x : uv.y;
    float age = floor(t * float(p_frames - 1) + 0.5);
    if (age < 0.5) return texture2D(u_input, uv);
    return historyFrame(age, uv);
}
