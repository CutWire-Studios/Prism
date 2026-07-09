// LED Matrix Equalizer
// Connect a clip's AudioOut to this shader's ShaderAudioIn for real FFT data.
//
// Classic green -> yellow -> red LED block equalizer.
// Falls back to time-based motion when u_hasAudio is false.

#ifdef GL_ES
precision mediump float;
#endif

uniform vec2 u_resolution;
uniform float u_time;
uniform sampler2D u_spectrum;
uniform float u_audioLevel;
uniform float u_beat;
uniform bool u_hasAudio;

const float COLS = 20.0;
const float ROWS = 13.0;

float spectrumAt(float x) {
    return texture2D(u_spectrum, vec2(clamp(x, 0.0, 1.0), 0.5)).r;
}

vec3 rowColor(float f) {
    // green -> yellow -> orange -> red
    vec3 c = mix(vec3(0.1, 0.85, 0.2), vec3(0.9, 0.9, 0.1), smoothstep(0.30, 0.60, f));
    c = mix(c, vec3(1.0, 0.5, 0.05), smoothstep(0.60, 0.82, f));
    c = mix(c, vec3(0.9, 0.1, 0.05), smoothstep(0.82, 1.0, f));
    return c;
}

void main() {
    vec2 uv = gl_FragCoord.xy / u_resolution.xy;
    uv.x *= COLS;
    uv.y = uv.y * ROWS;

    float col = floor(uv.x);
    float row = floor(uv.y);
    vec2 cell = fract(vec2(uv.x, uv.y)) * 2.0 - 1.0;

    float level;
    if (u_hasAudio) {
        float m = spectrumAt((col + 0.5) / COLS);
        level = m * (0.75 + u_audioLevel * 0.45) * ROWS;
    } else {
        float t = u_time * 3.0;
        level = (0.35 + 0.3 * sin(col * 0.9 + t) * cos(col * 0.31 - t * 0.7)
                      + 0.15 * sin(t * 1.7 + col)) * ROWS;
    }

    // rounded square LED with a gap between cells
    float d = max(abs(cell.x), abs(cell.y));
    float led = smoothstep(0.82, 0.72, d);

    float rowF = (row + 0.5) / ROWS;
    vec3 onColor = rowColor(rowF);

    float lit = step(row, level - 0.5);
    float topCell = step(abs(row - floor(level - 0.5)), 0.0);
    float brightness = lit * (0.85 + topCell * 0.35 + u_beat * 0.15);

    vec3 color = onColor * led * brightness;
    color += onColor * led * (1.0 - lit) * 0.05;   // faint "off" LEDs

    gl_FragColor = vec4(color, 1.0);
}
