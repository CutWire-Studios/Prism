// Cyberpunk Horizon Grid
// Synthwave style perspective grid with neon pulsing waves flowing forward.

#ifdef GL_ES
precision mediump float;
#endif

uniform vec2 u_resolution;
uniform float u_time;

// CUSTOMIZATION VARIABLES
const float TEMPO = 2.0;             // Speed of forward movement
const vec3 GRID_COLOR = vec3(0.0, 1.0, 0.4); // Neon Green
const float GLOW = 0.015;            // Bloom intensity

void main() {
    vec2 uv = (gl_FragCoord.xy * 2.0 - u_resolution.xy) / u_resolution.y;

    // Perspective transformation
    if (uv.y < -0.1) {
        float z = 1.0 / (-uv.y - 0.1);
        float x = uv.x * z;

        // Grid calculations
        float t = u_time * TEMPO;
        float gridX = abs(fract(x * 2.0 + 0.5) - 0.5) / z;
        float gridY = abs(fract(z - t) - 0.5) / z;

        // Neon lines with blooming logic
        float lineX = GLOW / gridX;
        float lineY = GLOW / gridY;

        // Fade out towards the horizon
        float horizonFade = smoothstep(0.0, 0.5, -uv.y - 0.1);
        vec3 finalColor = GRID_COLOR * (lineX + lineY) * horizonFade;

        gl_FragColor = vec4(finalColor, 1.0);
    } else {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0); // Black sky
    }
}
