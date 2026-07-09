// Particle Terrain
// Connect a clip's AudioOut to this shader's ShaderAudioIn for real FFT data.
//
// A 3D landscape of glowing dots, its surface rippled by the spectrum:
// warm white peaks fading to rose at the edges. Falls back to time-based
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

const int ROWS = 34;
const float DOT_SPACING = 0.075;
const float HORIZON = 0.78;
const float CAM_HEIGHT = 0.62;

float spectrumAt(float x) {
    return texture2D(u_spectrum, vec2(clamp(x, 0.0, 1.0), 0.5)).r;
}

float terrainHeight(float wx, float wz) {
    float t = u_time * 0.9;
    float h = 0.10 * sin(wx * 1.7 + wz * 0.8 + t)
            + 0.06 * sin(wx * 3.1 - wz * 1.3 - t * 1.4)
            + 0.04 * sin(wz * 2.2 + t * 0.7);
    if (u_hasAudio) {
        // low frequencies swell the middle, highs ripple the rim
        float f = clamp(abs(wx) * 0.35 + wz * 0.04, 0.0, 1.0);
        h += spectrumAt(f) * (0.30 + u_audioLevel * 0.25);
        h += 0.05 * sin(wx * 6.0 + t * 3.0) * u_bands.z;
        h *= 0.8 + u_beat * 0.4;
    }
    return h;
}

void main() {
    vec2 uv = gl_FragCoord.xy / u_resolution.xy;
    uv.x = (uv.x * 2.0 - 1.0) * (u_resolution.x / u_resolution.y);

    vec3 color = vec3(0.0);

    for (int i = 0; i < ROWS; i++) {
        float wz = 1.0 + float(i) * 0.28;
        float scale = 1.0 / wz;

        // nearest dot column in world space
        float wx = uv.x * wz;
        float col = floor(wx / DOT_SPACING + 0.5) * DOT_SPACING;

        float h = terrainHeight(col, wz);
        vec2 dotPos = vec2(col * scale, HORIZON - (CAM_HEIGHT - h) * scale);

        float d = length(vec2(uv.x, uv.y) - dotPos);
        float size = 0.0016 * (0.4 + scale);
        float glow = size / (d + 0.001);
        glow *= glow;

        // dim with distance, fog near the horizon
        float fade = scale * smoothstep(HORIZON, HORIZON - 0.12, dotPos.y);

        // warm white core, rose toward the sides, cool tint on high peaks
        vec3 tint = mix(vec3(1.0, 0.96, 0.90), vec3(1.0, 0.55, 0.55),
                        clamp(abs(col) * 0.45, 0.0, 1.0));
        tint = mix(tint, vec3(0.75, 0.82, 1.0), clamp(h * 1.8, 0.0, 0.6));

        color += tint * glow * fade * (0.8 + h * 1.2);
    }

    color *= 1.0 + u_beat * 0.2;
    gl_FragColor = vec4(color, 1.0);
}
