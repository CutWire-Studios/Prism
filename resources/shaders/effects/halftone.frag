// Dot coverage of one screen rotated by `ang`. chan: 0 = luma (mono), 1 = red, 2 = green, 3 = blue; darker means bigger dot.
float dotMask(vec2 uv, float ang, int chan) {
    mat2 m = rot2(ang);
    vec2 q = m * (uv * u_resolution) / p_size;
    vec2 cc = floor(q) + 0.5;
    vec2 suv = rot2(-ang) * (cc * p_size) / u_resolution;
    vec3 s = texture2D(u_input, clamp(suv, 0.0, 1.0)).rgb;
    float v = s.r;
    if (chan == 0) v = dot(s, vec3(0.2126, 0.7152, 0.0722));
    else if (chan == 2) v = s.g;
    else if (chan == 3) v = s.b;
    float r = sqrt(1.0 - v) * 0.75;
    float d = length(q - cc);
    return (1.0 - smoothstep(r - 0.08, r + 0.08, d)) * smoothstep(0.0, 0.1, r);
}

vec4 effect(vec2 uv) {
    vec4 col = texture2D(u_input, uv);
    if (p_mode == 0) {
        float m = dotMask(uv, radians(p_angle), 0);
        return vec4(mix(vec3(0.98, 0.96, 0.9), vec3(0.1, 0.1, 0.12), m), col.a);
    }
    float c = dotMask(uv, radians(p_angle + 15.0), 1);
    float mg = dotMask(uv, radians(p_angle + 75.0), 2);
    float y = dotMask(uv, radians(p_angle), 3);
    vec3 rgb = vec3(1.0);
    rgb *= mix(vec3(1.0), vec3(0.0, 1.0, 1.0), c);
    rgb *= mix(vec3(1.0), vec3(1.0, 0.0, 1.0), mg);
    rgb *= mix(vec3(1.0), vec3(1.0, 1.0, 0.0), y);
    return vec4(rgb, col.a);
}
