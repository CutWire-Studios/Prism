// Radial Spectrum Ring
// Connect a clip's AudioOut to this shader's ShaderAudioIn for real FFT data.
//
// FFT bars radiate outward from a circle; ambient particles drift behind.
// Falls back to time-based motion when u_hasAudio is false.

#ifdef GL_ES
precision mediump float;
#endif

uniform vec2 u_resolution;
uniform float u_time;
uniform sampler2D u_spectrum;
uniform float u_audioLevel;
uniform vec3 u_bands;
uniform float u_beat;
uniform bool u_hasAudio;

const float BARS = 72.0;
const float PI = 3.14159265;
const vec3 GLOW_COLOR = vec3(0.1, 0.85, 1.0);

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float spectrumAt(float x) {
    return texture2D(u_spectrum, vec2(clamp(x, 0.0, 1.0), 0.5)).r;
}

float barMagnitude(float idx) {
    float f = idx / BARS;
    if (u_hasAudio) {
        // mirror: low frequencies at the top, sweeping down both sides
        float m = abs(f * 2.0 - 1.0);
        return spectrumAt(m) * (0.7 + u_audioLevel * 0.5);
    }
    return 0.25 + 0.25 * sin(idx * 0.83 + u_time * 2.0)
                * cos(idx * 0.37 - u_time * 1.3);
}

void main() {
    vec2 uv = (gl_FragCoord.xy * 2.0 - u_resolution.xy) / u_resolution.y;
    float radius = length(uv);
    float angle = atan(uv.x, uv.y);   // 0 at top

    float beat = u_hasAudio ? u_beat : 0.5 + 0.5 * sin(u_time * 3.0);
    float ringR = 0.42 + beat * 0.03;

    // spectrum bars around the ring
    float slot = (angle / (2.0 * PI) + 0.5) * BARS;
    float idx = floor(slot);
    float localA = fract(slot) - 0.5;

    float mag = barMagnitude(idx + 0.5);
    float barLen = 0.04 + mag * 0.42;

    float angularWidth = smoothstep(0.42, 0.30, abs(localA));
    float inBar = smoothstep(ringR, ringR + 0.01, radius)
                * smoothstep(ringR + barLen, ringR + barLen - 0.02, radius);
    float bar = inBar * angularWidth;

    // thin base circle
    float circle = 0.006 / (abs(radius - ringR) + 0.004);

    // drifting glow particles
    float particles = 0.0;
    for (int i = 0; i < 2; i++) {
        float fi = float(i);
        vec2 grid = uv * (3.0 + fi * 2.0) + vec2(fi * 7.3, u_time * (0.05 + fi * 0.04));
        vec2 cell = floor(grid);
        vec2 pos = fract(grid) - 0.5;
        vec2 jitter = vec2(hash(cell), hash(cell + 19.7)) - 0.5;
        float d = length(pos - jitter * 0.6);
        float twinkle = 0.5 + 0.5 * sin(u_time * 2.0 + hash(cell + 5.1) * 20.0);
        particles += (0.006 + beat * 0.004) / (d + 0.02) * twinkle * 0.35;
    }

    vec3 color = GLOW_COLOR * (bar * (1.2 + mag) + circle * (0.5 + beat * 0.5));
    color += GLOW_COLOR * particles * (0.4 + u_bands.z * 0.6);
    color += vec3(0.02, 0.05, 0.08) * (1.0 - radius * 0.6);

    gl_FragColor = vec4(color, 1.0);
}
