float sdBox(vec2 p, vec2 b) {
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

float sdTriangle(vec2 p, float r) {
    float k = 1.7320508;
    p.x = abs(p.x) - r;
    p.y = p.y + r / k;
    if (p.x + k * p.y > 0.0)
        p = vec2(p.x - k * p.y, -k * p.x - p.y) * 0.5;
    p.x -= clamp(p.x, -2.0 * r, 0.0);
    return -length(p) * sign(p.y);
}

float sdStar(vec2 p, float r, float n, float m) {
    float an = 3.1415927 / n;
    float en = 3.1415927 / m;
    vec2 acs = vec2(cos(an), sin(an));
    vec2 ecs = vec2(cos(en), sin(en));
    float bn = mod(atan(p.x, p.y), 2.0 * an) - an;
    p = length(p) * vec2(cos(bn), abs(sin(bn)));
    p -= r * acs;
    p += ecs * clamp(-dot(p, ecs), 0.0, r * acs.y / ecs.y);
    return length(p) * sign(p.x);
}

vec4 generate(vec2 uv) {
    float aspect = u_resolution.x / u_resolution.y;
    vec2 p = rot2(radians(p_spin) * u_time) * ((uv - p_center) * vec2(aspect, 1.0));
    float r = p_size * (1.0 + p_pulse * sin(u_time * 2.0));
    float d;
    if (p_shape == 0)
        d = length(p) - r;
    else if (p_shape == 1)
        d = sdBox(p, vec2(r * 0.85));
    else if (p_shape == 2)
        d = sdTriangle(p, r);
    else if (p_shape == 3)
        d = sdStar(p, r, 5.0, 3.0);
    else
        d = abs(length(p) - r) - p_thickness;
    float m = 1.0 - smoothstep(0.0, max(p_softness, 0.0015), d);
    return vec4(mix(p_color_bg.rgb, p_color_fg.rgb, m), 1.0);
}
