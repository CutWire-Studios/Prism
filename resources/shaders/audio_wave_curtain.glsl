// Neon Wave Curtain
// Connect a clip's AudioOut to this shader's ShaderAudioIn for real FFT data.
//
// A dense curtain of glowing lines folding in slow waves, violet sweeping to
// hot pink; the spectrum drives the fold depth. Falls back to time-based
// motion when u_hasAudio is false.

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

const float LINES = 46.0;
const vec3 BG_COLOR = vec3(0.09, 0.04, 0.17);

float spectrumAt(float x) {
    return texture2D(u_spectrum, vec2(clamp(x, 0.0, 1.0), 0.5)).r;
}

void main() {
    vec2 uv = gl_FragCoord.xy / u_resolution.xy;
    float t = u_time * 0.8;

    // fold amplitude: spectrum band under each part of the curtain
    float amp;
    if (u_hasAudio) {
        amp = 0.4 + spectrumAt(uv.x) * 1.6 + u_bands.x * 0.6;
        amp *= 0.7 + u_audioLevel * 0.6;
    } else {
        amp = 0.8 + 0.4 * sin(t * 0.9 + uv.x * 3.0);
    }

    // large slow folds plus a finer ripple displace the line field
    float wave = sin(uv.y * 5.0 + uv.x * 3.5 + t) * 0.045 * amp
               + sin(uv.y * 11.0 - uv.x * 2.0 - t * 1.6) * 0.018 * amp
               + sin(uv.y * 2.2 + t * 0.5) * 0.03;

    float x = uv.x + wave;

    // thin glowing lines, spacing-normalized for even thickness
    float d = abs(fract(x * LINES) - 0.5) / LINES;
    float line = 0.0018 / (d + 0.0012);

    // bright bands where the folds bunch the curtain together
    float sheen = 0.55 + 0.45 * sin(uv.y * 5.0 + uv.x * 3.5 + t + 1.6);
    sheen = 0.4 + 0.6 * sheen * sheen;

    // violet -> magenta -> hot pink across the curtain
    vec3 tint = mix(vec3(0.55, 0.30, 0.95), vec3(0.95, 0.20, 0.75),
                    smoothstep(0.15, 0.75, uv.x));
    tint = mix(tint, vec3(1.0, 0.25, 0.45), smoothstep(0.75, 1.0, uv.x));

    vec3 color = BG_COLOR * (0.7 + 0.3 * uv.y);
    color += tint * line * sheen * (0.7 + u_beat * 0.4);
    color += tint * 0.08 * amp * (1.0 - abs(uv.y - 0.5) * 1.4);

    gl_FragColor = vec4(color, 1.0);
}
