// Geometric Portal Matrix
// Concentric squares creating a tunnel effect scaling infinitely into view.

#ifdef GL_ES
precision mediump float;
#endif

uniform vec2 u_resolution;
uniform float u_time;

// CUSTOMIZATION VARIABLES
const float TEMPO = 1.4;             // Tunnel advancement rate
const vec3 PORTAL_COLOR = vec3(1.0, 0.9, 0.0); // Neon Gold/Yellow

void main() {
    vec2 uv = (gl_FragCoord.xy * 2.0 - u_resolution.xy) / u_resolution.y;
    float t = u_time * TEMPO;

    // Chebyshev distance creates square geometry
    float boxDist = max(abs(uv.x), abs(uv.y));

    // Create repeating scaling waves
    float scaleLog = log(boxDist) - t;
    float lines = abs(sin(scaleLog * 4.0));

    // Intensify line contrast to establish bloom boundaries
    float neonGlow = 0.015 / (lines + 0.01);

    // Apply Vignette dark edge to center accentuation
    neonGlow *= smoothstep(0.0, 0.7, boxDist);

    gl_FragColor = vec4(PORTAL_COLOR * neonGlow, 1.0);
}
