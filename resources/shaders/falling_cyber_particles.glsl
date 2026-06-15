// Falling Cyber Particles
// Shimmering digital rain with intense neon halos.

#ifdef GL_ES
precision mediump float;
#endif

uniform vec2 u_resolution;
uniform float u_time;

// CUSTOMIZATION VARIABLES
const float TEMPO = 1.2;             // Fall speed multiplier
const vec3 PARTICLE_COLOR = vec3(0.5, 0.0, 1.0); // Neon Purple

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

void main() {
    vec2 uv = gl_FragCoord.xy / u_resolution.xy;
    float t = u_time * TEMPO;

    // Grid alignment for particles
    vec2 grid = vec2(30.0, 15.0);
    vec2 ipos = floor(uv * grid);
    vec2 fpos = fract(uv * grid) - 0.5;

    // Give each column a different speed and offset
    float rand = hash(vec2(ipos.x, 12.3));
    float yOffset = fract(t * (0.5 + rand * 0.5) + rand);

    // Calculate distance to animated particle point
    vec2 pCenter = vec2(0.0, (yOffset * 2.0 - 1.0));
    float dist = length(fpos - pCenter);

    // Generate bloom around the dot
    float glow = 0.04 / (dist + 0.01);
    glow *= smoothstep(1.0, 0.0, dist); // Boundary cutoff

    vec3 color = PARTICLE_COLOR * glow;
    gl_FragColor = vec4(color, 1.0);
}
