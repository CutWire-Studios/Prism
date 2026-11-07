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

## Custom generator params

A custom generator (Input node with a GLSL source) can declare inspector controls in the first
`/* @params ... */` comment, a JSON array of entries in the `effects.json` param format:

```glsl
/* @params
[ { "name": "amount", "type": "float", "label": "Amount", "min": 0, "max": 1, "default": 0.5 } ]
*/
uniform float p_amount;
```

Each param is set as the uniform `p_<name>`, which the shader declares itself. Values live in the
node's `params` and default to the header's `default`. "Edit as custom" on a preset generator
produces such a program with the current values as defaults.

## Inline Lua

A custom generator can carry its own Lua script (node params `lua`, `luaRunOn`, `luaIntervalMs`;
default run on a 33 ms interval). Its `input` is the JSON from the wired data port and the table it
returns is mapped to uniforms as above. The script output and the wired data are merged into one
object; on key conflicts the wired data wins. Without inline Lua nothing changes.

## Shader process node

The Shader node (Video FX) runs a fragment shader you write on its input frame. The code is compiled
as written, so it declares its own precision, varying, uniforms and `main()`; there is no `#version`
line (GLSL 1.10 style: `texture2D`, `gl_FragColor`). A compile error passes the input through
unchanged and is shown in the node's editor.

Coordinates: use the varying `v_uv` (`varying vec2 v_uv;`), 0..1 across the frame with (0,0) at the
top left. `gl_FragCoord` is vertically flipped relative to the image, so do not sample with it.

The app binds, when the code declares them:

| Uniform | Meaning |
|---|---|
| `uniform sampler2D u_input` | the incoming frame |
| `uniform vec2 u_texel` | 1 / frame size |
| `uniform vec2 u_resolution` | frame size in pixels |
| `uniform float u_time` | seconds since the node started |
| `uniform sampler2D u_input1` ... `u_inputN` | extra video inputs; N is the highest index declared (at most 8) and the node gets contiguous ports `in1`..`inN`. An unwired port reads as transparent |
| `uniform sampler2D u_prev` | this node's previous output |
| `uniform sampler2D u_history` | the last frames of the input, see below |
| `uniform float p_<name>` ... | the `@params` controls, as for custom generators |
| `u_<key>`, `u_bands`, `u_audioLevel`, `u_spectrum`, ... | the data port, mapped as in the sections above |

The node has a `data` input port after the video ports; wired and inline data are merged and mapped
to uniforms exactly like a custom generator's. Changing which `u_input<N>` the code declares adds or
removes ports; connections to removed ports are dropped.

Besides an array, the `@params` header may be an object that also sets the history length
(1..32, default 8 when `u_history` is declared):

```glsl
/* @params
{ "params": [ { "name": "amount", "type": "float", "min": 0, "max": 1, "default": 0.5 } ],
  "history": 8 }
*/
```

`u_history` is an atlas of the last frames; declare its helpers and read frames with `historyFrame`
(age 1 is the previous frame):

```glsl
uniform sampler2D u_history;
uniform vec4 u_historyInfo;   // cols, rows, ring size, next slot to write
uniform vec2 u_historyAtlas;  // atlas size in px
uniform vec2 u_historyTile;   // tile size in px
vec4 historyFrame(float age, vec2 uv) {
    float n = u_historyInfo.z;
    float slot = mod(u_historyInfo.w + n - clamp(floor(age + 0.5), 1.0, n), n);
    float row = floor((slot + 0.5) / u_historyInfo.x);
    float col = slot - row * u_historyInfo.x;
    vec2 px = vec2(col, row) * u_historyTile
            + clamp(uv * u_historyTile, vec2(0.5), u_historyTile - 0.5);
    return texture2D(u_history, px / u_historyAtlas);
}
```

The code is stored in the node's `code` param and the control values in `params`. Over MCP,
`add_process_node` / `set_process_params` accept it as `params.code` (effect `shader`, id 67).
