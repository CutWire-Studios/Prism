// Audio-Pulse VU Bar Array
// Classic equalizer visualizer simulation with dramatic bloom.

#ifdef GL_ES
precision mediump float;
#endif

uniform vec2 u_resolution;
uniform float u_time;

// CUSTOMIZATION VARIABLES
const float TEMPO = 4.0;             // Bounce frequency
const vec3 BAR_COLOR = vec3(0.2, 1.0, 0.5); // Acid Lime Green

void main() {
    vec2 uv = gl_FragCoord.xy / u_resolution.xy;
    float t = u_time * TEMPO;

    // Split grid layout into horizontal blocks
    float columns = 16.0;
    float barIdx = floor(uv.x * columns);
    float localX = fract(uv.x * columns) - 0.5;

    // Height calculation based on procedural column logic
    float peakHeight = 0.2 + 0.6 * sin(barIdx * 45.12 + t) * cos(barIdx * 12.75 + t * 0.3);
    peakHeight = max(0.1, peakHeight); // Minimum baseline height

    // Render boundaries with falloff illumination
    float distX = abs(localX) - 0.35;
    float distY = uv.y - peakHeight;

    float clampX = smoothstep(0.0, -0.05, distX);
    float bloomY = 0.015 / (abs(distY) + 0.005);

    // Merge elements cleanly on a strict true black setting
    float renderEngine = clampX * (uv.y < peakHeight ? 2.0 : bloomY);

    gl_FragColor = vec4(BAR_COLOR * renderEngine * (0.6 + uv.y), 1.0);
}
