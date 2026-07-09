// Spectral Bloom
// Connect a clip's AudioOut to this shader's ShaderAudioIn for real FFT data.
//
// Polar blob whose silhouette is carved by the FFT: hot pink core fading to
// a cool blue rim, spikes riding the spectrum. Falls back to time-based
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

const float PI = 3.14159265;

float spectrumAt(float x) {
    return texture2D(u_spectrum, vec2(clamp(x, 0.0, 1.0), 0.5)).r;
}

float blobRadius(float angle) {
    float base = 0.34;
    if (u_hasAudio) {
        // mirrored so left/right match; smoothed over three taps
        float f = abs(fract(angle / (2.0 * PI) + 0.5) * 2.0 - 1.0);
        float m = spectrumAt(f) * 0.5
                + spectrumAt(f + 0.015) * 0.25
                + spectrumAt(max(f - 0.015, 0.0)) * 0.25;
        return base + u_bands.x * 0.06 + u_beat * 0.03 + m * 0.30;
    }
    float t = u_time * 1.5;
    return base + 0.05 * sin(angle * 7.0 + t)
                + 0.03 * sin(angle * 13.0 - t * 1.4)
                + 0.02 * sin(t * 2.0);
}

void main() {
    vec2 uv = (gl_FragCoord.xy * 2.0 - u_resolution.xy) / u_resolution.y;
    float radius = length(uv);
    float angle = atan(uv.x, uv.y);

    float r = blobRadius(angle);
    float edge = radius - r;

    float inside = smoothstep(0.01, -0.01, edge);
    float rim = 0.010 / (abs(edge) + 0.008);

    float energy = u_hasAudio ? u_audioLevel : 0.4 + 0.2 * sin(u_time * 2.0);

    // radial gradient: hot pink core -> blue rim
    float coreF = clamp(radius / max(r, 0.001), 0.0, 1.0);
    vec3 coreColor = mix(vec3(1.0, 0.15, 0.55), vec3(0.35, 0.55, 1.0),
                         smoothstep(0.15, 0.95, coreF));
    coreColor *= 0.55 + energy * 0.9;

    vec3 rimColor = mix(vec3(0.5, 0.6, 1.0), vec3(1.0, 0.3, 0.7), u_bands.z);

    vec3 color = coreColor * inside + rimColor * rim * (0.5 + u_beat * 0.8);
    color += vec3(0.03, 0.02, 0.06) * (1.0 - radius * 0.7);   // ambient haze

    gl_FragColor = vec4(color, 1.0);
}
