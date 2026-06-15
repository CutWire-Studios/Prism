// Audio-Reactive Visual Wave
// Sinuous plasma wave mimicking high-energy audio frequencies.

#ifdef GL_ES
precision mediump float;
#endif

uniform vec2 u_resolution;
uniform float u_time;

// CUSTOMIZATION VARIABLES
const float TEMPO = 3.0;             // Frequency modulation speed
const vec3 WAVE_COLOR = vec3(1.0, 0.3, 0.0); // Neon Electric Orange
const float THICKNESS = 0.02;

void main() {
    vec2 uv = (gl_FragCoord.xy * 2.0 - u_resolution.xy) / u_resolution.y;
    float t = u_time * TEMPO;

    // Wave formula using stacked sine functions
    float freq = uv.x * 4.0;
    float wave = sin(freq - t) * 0.3 + cos(freq * 2.3 + t * 1.5) * 0.15;

    // Dynamic distance mapping for neon bloom
    float dist = abs(uv.y - wave);
    float glow = THICKNESS / (dist + 0.01);

    // Add variations along the wave line
    vec3 finalColor = WAVE_COLOR * pow(glow, 1.5);
    finalColor.g += sin(uv.x + t) * 0.2; // Add color dynamics

    gl_FragColor = vec4(finalColor, 1.0);
}
