float lumAt(vec2 p) {
    return dot(texture2D(u_input, clamp(p, 0.0, 1.0)).rgb, vec3(0.2126, 0.7152, 0.0722));
}

vec4 effect(vec2 uv) {
    vec4 col = texture2D(u_input, uv);
    float a = radians(p_angle);
    vec2 d = vec2(cos(a), sin(a)) * 1.5 / u_resolution;
    float e = (lumAt(uv + d) - lumAt(uv - d)) * p_strength;
    return vec4(vec3(clamp(0.5 + e, 0.0, 1.0)), col.a);
}
