// Abstract Quantum Strings
// Intertwining chaotic lines tracing high-frequency movements.

#ifdef GL_ES
precision mediump float;
#endif

uniform vec2 u_resolution;
uniform float u_time;

// CUSTOMIZATION VARIABLES
const float TEMPO = 0.8;             // Speed of evolution
const vec3 COLOR_MIX = vec3(0.1, 1.0, 0.8); // Electric Mint

void main() {
    vec2 uv = (gl_FragCoord.xy * 2.0 - u_resolution.xy) / u_resolution.y;
    float t = u_time * TEMPO;

    float finalGlow = 0.0;

    // Build complex overlay patterns via loops
    for(float i = 1.0; i < 5.0; i++) {
        uv.x += sin(uv.y + t * i) * 0.3;
        uv.y += cos(uv.x + t * (i + 1.0)) * 0.2;

        float dist = abs(uv.x * uv.y);
        finalGlow += 0.01 / (dist + 0.005);
    }

    vec3 color = COLOR_MIX * finalGlow * 0.2;
    gl_FragColor = vec4(color, 1.0);
}
