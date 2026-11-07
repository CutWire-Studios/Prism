// p_lut is the 512x512 atlas produced by Lut::load: 8x8 tiles of 64x64, tile = blue index,
// red along x and green along y inside a tile. Sampled at texel centres.
vec2 lutUv(vec2 rg, float tile) {
    vec2 t = vec2(mod(tile, 8.0), floor(tile / 8.0));
    return (t * 64.0 + rg * 63.0 + 0.5) / 512.0;
}

vec4 effect(vec2 uv) {
    vec4 col = texture2D(u_input, uv);
    // A missing file is bound as a transparent texture; no LUT is fully opaque everywhere.
    if (texture2D(p_lut, vec2(0.5 / 512.0)).a < 0.5) return col;

    vec3 c = clamp(col.rgb, 0.0, 1.0);
    float b = c.b * 63.0;
    float b0 = floor(b);
    float b1 = min(b0 + 1.0, 63.0);
    vec3 s0 = texture2D(p_lut, lutUv(c.rg, b0)).rgb;
    vec3 s1 = texture2D(p_lut, lutUv(c.rg, b1)).rgb;
    vec3 graded = mix(s0, s1, b - b0);
    return vec4(mix(col.rgb, graded, clamp(p_amount, 0.0, 1.0)), col.a);
}
