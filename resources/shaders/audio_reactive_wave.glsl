// Audio-Reactive Visual Wave
// Connect a clip's AudioOut to this shader's ShaderAudioIn for real FFT data.
//
// Optional audio uniforms (fallback to time-based motion when u_hasAudio is false):
//   uniform sampler2D u_spectrum;   // 1xN, .r = magnitude 0..1, x = low→high freq
//   uniform float     u_audioLevel; // overall level 0..1
//   uniform bool      u_hasAudio;   // false when no audio connected

#ifdef GL_ES
precision mediump float;
#endif

uniform vec2 u_resolution;
uniform float u_time;
uniform sampler2D u_spectrum;
uniform float u_audioLevel;
uniform bool u_hasAudio;

const float TEMPO = 3.0;
const vec3 WAVE_COLOR = vec3(1.0, 0.3, 0.0);
const float THICKNESS = 0.02;

float spectrumAt(float x) {
    return texture2D(u_spectrum, vec2(clamp(x, 0.0, 1.0), 0.5)).r;
}

void main() {
    vec2 uv = (gl_FragCoord.xy * 2.0 - u_resolution.xy) / u_resolution.y;
    float t = u_time * TEMPO;

    float audioBoost = u_hasAudio ? (0.4 + u_audioLevel * 0.8) : 1.0;
    float specMod = u_hasAudio
        ? spectrumAt(uv.x * 0.5 + 0.5) * 0.6 + spectrumAt(abs(uv.x) * 0.3 + 0.1) * 0.4
        : 0.0;

    float freq = uv.x * 4.0;
    float wave = sin(freq - t) * (0.3 + specMod * 0.5) * audioBoost
               + cos(freq * 2.3 + t * 1.5) * (0.15 + specMod * 0.25) * audioBoost;

    if (!u_hasAudio) {
        wave = sin(freq - t) * 0.3 + cos(freq * 2.3 + t * 1.5) * 0.15;
    }

    float dist = abs(uv.y - wave);
    float glow = THICKNESS / (dist + 0.01);

    vec3 finalColor = WAVE_COLOR * pow(glow, 1.5);
    finalColor.g += (u_hasAudio ? specMod : sin(uv.x + t) * 0.2) * 0.5;

    gl_FragColor = vec4(finalColor, 1.0);
}
