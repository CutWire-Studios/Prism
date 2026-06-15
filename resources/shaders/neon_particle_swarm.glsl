// Neon Particle Swarm
// Soft-focused orb fields tracking circular trajectories.

#ifdef GL_ES
precision mediump float;
#endif

uniform vec2 u_resolution;
uniform float u_time;

// CUSTOMIZATION VARIABLES
const float TEMPO = 2.5;             // Swarm velocity
const vec3 STAR_COLOR = vec3(0.0, 0.8, 1.0); // Bright Turquoise

void main() {
    vec2 uv = (gl_FragCoord.xy * 2.0 - u_resolution.xy) / u_resolution.y;
    float t = u_time * TEMPO;

    vec3 layerAccumulation = vec3(0.0);

    for(float i = 1.0; i <= 6.0; i++) {
        // Pseudo-random orbits per virtual node
        vec2 nodePos = vec2(
            sin(t + i * 1.5) * (0.3 + i * 0.1),
            cos(t * 0.8 + i * 2.2) * (0.2 + i * 0.08)
        );

        float d = length(uv - nodePos);
        float brightness = 0.008 / (d * d + 0.002);

        layerAccumulation += STAR_COLOR * brightness;
    }

    gl_FragColor = vec4(layerAccumulation, 1.0);
}
