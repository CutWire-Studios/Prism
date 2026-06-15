// Cosmic Plasma Flares
// Organic, fluid-like glowing gas simulations filling empty space.

#ifdef GL_ES
precision mediump float;
#endif

uniform vec2 u_resolution;
uniform float u_time;

// CUSTOMIZATION VARIABLES
const float TEMPO = 1.0;             // Fluid flow modifier
const vec3 CORE_BURST = vec3(1.0, 0.1, 0.1); // Crimson Red
const vec3 GLOW_BURST = vec3(0.4, 0.0, 0.8); // Indigo Purple

void main() {
    vec2 uv = (gl_FragCoord.xy * 2.0 - u_resolution.xy) / u_resolution.y;
    float t = u_time * TEMPO;

    // Warp space over multiple octaves
    float angleMod = atan(uv.y, uv.x);
    float radiusMod = length(uv);

    float noisePattern = sin(radiusMod * 4.0 - t) + cos(angleMod * 3.0 + t);
    noisePattern += sin(uv.x * 3.0 + t) * cos(uv.y * 4.0 - t);

    // Isolate bright energy bands
    float band = abs(fract(noisePattern * 0.5) - 0.5);
    float bloom = 0.03 / (band + 0.02);

    vec3 mixColor = mix(CORE_BURST, GLOW_BURST, radiusMod * 0.5);
    gl_FragColor = vec4(mixColor * bloom * 0.4, 1.0);
}
