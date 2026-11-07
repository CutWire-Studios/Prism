# SwitchX Shader Data Interface (v1)

Shaders in SwitchX are **data-driven**: a shader node's input is a JSON object produced
by the script graph (audio-analyzer nodes, Lua script nodes — chained and
merged however you like). SwitchX maps that JSON to GLSL uniforms every frame.

A shader **declares only the uniforms it uses**. Anything not declared is ignored, and
any uniform with no matching data is simply left unset. None of this depends on how the
FFT (or anything else upstream) is implemented — tune an audio-analyzer node's FFT size,
bin count or band splits and the uniform contract below does not change.

## Built-in uniforms (always set)

| Uniform | GLSL type | Meaning |
|---|---|---|
| `u_resolution` | `vec2` | Output size in pixels |
| `u_time` | `float` | Seconds since the shader started |

> Do **not** send a JSON key named `time` — it would collide with `u_time`. Script
> nodes with no code emit `epoch` (wall-clock seconds) instead.

## Audio block (set whenever an audio-analyzer node is anywhere upstream)

These are a **stable, named contract**. An audio-analyzer node always emits the full set,
regardless of its FFT configuration.

| Uniform | GLSL type | Range | Meaning |
|---|---|---|---|
| `u_hasAudio` | `bool` | — | True while audio is flowing |
| `u_audioLevel` | `float` | 0..1 | Smoothed overall loudness (RMS) |
| `u_bands` | `vec3` | 0..1 | `(low, mid, high)` energy |
| `u_bass` | `float` | 0..1 | Same as `u_bands.x` |
| `u_mid` | `float` | 0..1 | Same as `u_bands.y` |
| `u_treble` | `float` | 0..1 | Same as `u_bands.z` |
| `u_beat` | `float` | 0..1 | Onset pulse; spikes on a beat then decays |
| `u_spectrum` | `sampler2D` | 0..1 | FFT magnitude; sample `x∈[0,1]` = low→high, read `.r` |
| `u_spectrumSize` | `int` | — | Number of valid spectrum bins |

`u_beatPulse` is kept as a legacy alias of `u_beat`.

Sampling the spectrum:

```glsl
uniform sampler2D u_spectrum;
uniform int u_spectrumSize;
// magnitude at normalized frequency f (0 = lowest, 1 = highest):
float mag = texture(u_spectrum, vec2(f, 0.5)).r;
```

## Generic block (any script's JSON keys)

Every key in the input JSON is also exposed generically as `u_<key>`, so a Lua script can
drive arbitrary uniforms without SwitchX knowing about them:

| JSON value | Becomes |
|---|---|
| number | `float` (`u_<key>`) |
| bool | `bool` |
| `[a, b]` / `[a,b,c]` / `[a,b,c,d]` | `vec2` / `vec3` / `vec4` |
| `{x,y}` / `{x,y,z}` / `{x,y,z,w}` | `vec2` / `vec3` / `vec4` |
| larger number array | ignored generically (use the `spectrum` key for a texture) |

Keys that already start with `u_` are used verbatim (no second prefix).

### Example

A Lua script returns:

```lua
return { intensity = 0.8, tint = { 1.0, 0.2, 0.4 }, hue = 210 }
```

The shader can then declare and use:

```glsl
uniform float u_intensity;   // 0.8
uniform vec3  u_tint;        // (1.0, 0.2, 0.4)
uniform float u_hue;         // 210.0
```

Wire an audio-analyzer node into the same shader (fan-in merges both JSON objects into one
`input`) and the shader additionally gets `u_audioLevel`, `u_bands`, `u_spectrum`, etc.
