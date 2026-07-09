// Spectrum Mirror Skyline
// Connect a clip's AudioOut to this shader's ShaderAudioIn for real FFT data.
//
// Thin neon frequency needles over a dark floor reflection, hue sweeping
// warm lows to cool highs. Falls back to time-based motion when u_hasAudio
// is false.

#ifdef GL_ES
precision mediump float;
#endif

uniform vec2 u_resolution;
uniform float u_time;
uniform sampler2D u_spectrum;
uniform float u_audioLevel;
uniform float u_beat;
uniform bool u_hasAudio;

const float NEEDLES = 140.0;
const float BASELINE = 0.30;

float hash(float n) {
    return fract(sin(n * 127.1) * 43758.5453);
}

float spectrumAt(float x) {
    return texture2D(u_spectrum, vec2(clamp(x, 0.0, 1.0), 0.5)).r;
}

vec3 hueSweep(float x) {
    // amber -> red -> magenta -> violet -> blue -> cyan
    vec3 c = mix(vec3(1.0, 0.65, 0.1), vec3(1.0, 0.2, 0.3), smoothstep(0.05, 0.30, x));
    c = mix(c, vec3(0.85, 0.2, 1.0), smoothstep(0.30, 0.55, x));
    c = mix(c, vec3(0.3, 0.35, 1.0), smoothstep(0.55, 0.78, x));
    c = mix(c, vec3(0.25, 0.8, 1.0), smoothstep(0.78, 1.0, x));
    return c;
}

float needleField(vec2 uv) {
    float slot = uv.x * NEEDLES;
    float idx = floor(slot);
    float localX = fract(slot) - 0.5;
    float fx = (idx + 0.5) / NEEDLES;

    float h;
    if (u_hasAudio) {
        float m = spectrumAt(fx);
        // per-needle jitter gives the dense forest-of-lines look
        h = m * (0.45 + hash(idx) * 0.5) * (0.8 + u_audioLevel * 0.6) + m * u_beat * 0.15;
    } else {
        float t = u_time * 2.0;
        h = (0.2 + 0.2 * sin(fx * 21.0 + t) * cos(fx * 47.0 - t * 0.6))
          * (0.5 + hash(idx) * 0.5);
    }
    h = max(h, 0.015);

    float y = uv.y - BASELINE;
    if (y < 0.0) return 0.0;

    float core = smoothstep(0.20, 0.05, abs(localX)) * step(y, h);
    float tipGlow = 0.004 / (abs(y - h) + 0.006) * smoothstep(0.35, 0.05, abs(localX));
    float fade = 0.35 + 0.65 * (1.0 - y / max(h, 0.001));

    return core * fade + tipGlow * 0.6;
}

void main() {
    vec2 uv = gl_FragCoord.xy / u_resolution.xy;
    vec3 tint = hueSweep(uv.x);

    float direct = needleField(uv);

    // floor reflection: mirror below the baseline, stretched and dimmed
    vec2 muv = vec2(uv.x, BASELINE + (BASELINE - uv.y) * 1.4);
    float refl = needleField(muv) * 0.30 * smoothstep(0.0, 0.25, uv.y);

    // glowing baseline
    float base = 0.0035 / (abs(uv.y - BASELINE) + 0.004);

    vec3 color = tint * (direct + refl) * (1.0 + u_beat * 0.25);
    color += tint * base * 0.5;
    color += vec3(0.01, 0.005, 0.02);

    gl_FragColor = vec4(color, 1.0);
}
