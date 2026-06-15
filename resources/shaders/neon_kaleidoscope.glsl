// Neon Kaleidoscope
// Highly energetic, symmetrical neon geometry perfect for rhythmic drops.

#ifdef GL_ES
precision mediump float;
#endif

uniform vec2 u_resolution;
uniform float u_time;

// CUSTOMIZATION VARIABLES
const float TEMPO = 1.5;             // Speed of evolution
const vec3 COLOR_A = vec3(1.0, 0.0, 0.5); // Neon Pink
const vec3 COLOR_B = vec3(0.0, 1.0, 1.0); // Neon Cyan
const float SIDES = 8.0;             // Kaleidoscope segments

void main() {
    vec2 uv = (gl_FragCoord.xy * 2.0 - u_resolution.xy) / u_resolution.y;
    float t = u_time * TEMPO;

    // Convert to polar coordinates for kaleidoscope effect
    float r = length(uv);
    float a = atan(uv.y, uv.x);

    // Segment the angle
    a = mod(a, 2.0 * 3.14159 / SIDES);
    a = abs(a - 3.14159 / SIDES);

    // Reconstruct UVs
    vec2 uv_k = r * vec2(cos(a), sin(a));

    // Create animated neon rings
    float wave = sin(uv_k.x * 10.0 - t) * 0.5 + 0.5;
    float line = 0.005 / abs(uv_k.y - wave * 0.2);

    // Bloom and color mix
    vec3 color = mix(COLOR_A, COLOR_B, sin(t * 0.5) * 0.5 + 0.5) * line;
    color += mix(COLOR_B, COLOR_A, r) * (line * 0.5); // Inner glow

    gl_FragColor = vec4(color, 1.0);
}
