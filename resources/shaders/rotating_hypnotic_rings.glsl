// Rotating Hypnotic Rings
// Concentric nested structures that spin and warp to the beat.

#ifdef GL_ES
precision mediump float;
#endif

uniform vec2 u_resolution;
uniform float u_time;

// CUSTOMIZATION VARIABLES
const float TEMPO = 2.0;             // Speed of rotation and pulsing
const vec3 CORE_COLOR = vec3(0.0, 0.4, 1.0); // Neon Electric Blue
const vec3 EDGE_COLOR = vec3(1.0, 0.0, 0.4); // Neon Magenta

void main() {
    vec2 uv = (gl_FragCoord.xy * 2.0 - u_resolution.xy) / u_resolution.y;
    float t = u_time * TEMPO;

    float dist = length(uv);
    float angle = atan(uv.y, uv.x);

    // Create broken geometric segments
    float segments = sin(angle * 6.0 + t) * cos(angle * 2.0 - t);

    // Generate expanding concentric rings
    float ringPattern = sin(dist * 25.0 - t * 4.0);
    float activeRings = smoothstep(0.8, 0.95, ringPattern) * smoothstep(0.2, 0.6, segments + 0.5);

    // Sharp edge with heavy bloom falloff
    float glow = activeRings * 0.2 / abs(fract(dist * 5.0 - t) - 0.5);

    vec3 finalColor = mix(CORE_COLOR, EDGE_COLOR, dist) * glow;
    gl_FragColor = vec4(finalColor, 1.0);
}
