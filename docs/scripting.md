# Scripting

Prism runs Lua 5.4 in Script nodes and in parameter expressions. Both run off the GUI thread
in a sandbox, one runtime per scene. A scene's scripts and expressions only run while the scene is
the Live scene, the Staging scene or the scene being edited.

## Script node

A Script node runs its code and publishes the returned table as JSON. Wire its output to the
data input of a Text or Shader source (or to another Script) to use the values there. The
upstream JSON arrives in the global `input` table.

Run on (inspector):

| Mode | Behaviour |
| --- | --- |
| Input change | Runs whenever an upstream node's output changes. |
| Interval | Runs every N ms (minimum 20). |
| Time of day | Runs once a day at the first check at or after `HH:mm:ss`. |
| Manual | Runs only from the Run button on the node, the context menu or the inspector. |

A script with no code publishes `{tick, epoch, pulse = true}` on every run, which makes an
empty Script node a timer or clock pulse.

A failed run turns the node border red, shows a warning badge on the node and the full error
in the inspector. The next successful run clears it. `print(...)` and `prism.log(...)` go to a
per-node log (last 200 lines) shown in the inspector.

## Sandbox

Available: `base` (without `dofile`, `loadfile`; `load` accepts text chunks only), `string`,
`math`, `table`, plus `http.get(url, timeoutMs)`. Removed: `os`, `io`, `package`, `require`,
`debug`.

Each run is limited to 20 million VM instructions (200 thousand for an expression). On overrun
the run is aborted with an `instruction limit exceeded` error; the app keeps running.

## `prism` API

| Function | Description |
| --- | --- |
| `prism.param(nodeId, name)` | Reads a parameter of a node in this scene. `name` may be dotted for nested values, for example `"params.opacity"`. |
| `prism.param(nodeId, name, value)` | Sets it. The write is applied on the GUI thread without an undo entry. |
| `prism.audio.bands([analyzerId])` | `{low, mid, high, level, beat, hasAudio}` of an Audio Analyzer node (the most recently updated one without an id). Zeros when there is no audio. |
| `prism.bpm` | Tempo of the global BPM clock (default 120; set with the transport Tap button or saved in the session). |
| `prism.beat` | Beat position of the BPM clock as a float: beat count plus phase (`12.25` is a quarter of the way through beat 12). Read it at the moment you need it; it is not updated while a script runs. |
| `prism.log(...)` | Same as `print`. |
| `prism.time()`, `prism.clock()`, `prism.date(fmt, t)` | The `os.time`, `os.clock` and `os.date` equivalents. |

Scripts can only reach nodes of their own scene's graph; other node ids raise
`node N is not in this scene`. Node ids are shown in the inspector header.

## Parameter expressions

Numeric inspector parameters with an "f" button can be driven by a Lua expression instead of
a fixed value. Clicking "f" adds a socket for that parameter on the node and opens the expression
editor; the slider is replaced by a chip showing the source, the expression and its result.
Clicking "f" again removes the expression and the socket. Expressions are stored per node in
`params.expressions` (`{ "<param>": "<expr>" }`).

Expressions are evaluated at about 30 Hz while the scene's runtime is active. Available names:

- `input`: whatever is wired into the parameter's socket: a Modulator's number, an Audio
  Analyzer's `{low, mid, high, level, beat, hasAudio}` or a Script's output table. `nil` when
  nothing is connected. The editor lists each field with its live value and range
- `t`: seconds since the scene's runtime was created
- `bpm`, `beat`: the BPM clock's tempo and beat position, as `prism.bpm` / `prism.beat`
- `globals.<name>`: project globals
- everything in `math` through the global environment, for example `math.sin(t)`
- helpers: `map(x, inMin, inMax, outMin, outMax)` (clamped to the output range),
  `clamp(x, lo, hi)`, `lerp(a, b, t)`, `invlerp(a, b, x)` (0 at a, 1 at b) and
  `smoothstep(e0, e1, x)` (eased 0..1)

The result is written as the stored parameter value (for example opacity 0..1 even though the
inspector shows percent) and is not clamped. A syntax or runtime error, a non-numeric result
or an instruction overrun is shown under the field and leaves the last value in place.

## OSC control input

When the OSC server is enabled (service settings), every incoming message whose address is not under
`/prism/` stores its first numeric (or boolean) argument as that address's latest value; `/prism/...`
stays show control and is never stored. A Modulator input spec `osc:<address>` (for example
`osc:/fader/1`) reads that value, 0 until something arrives, so any modulator kind can take it as
`in`. The **Control In** modulator kind outputs `in` (read as 0..1, clamped by default) scaled to
`min`..`max`, which is the usual one-node setup for a fader or knob.

Learn fills the address in for you. In a modulator's inspector, **Learn** next to the input waits up
to 10 seconds for the next control message and writes `osc:<address>`. On a parameter's link
button, **Learn OSC...** creates a Control In modulator bound to that parameter (one undo step), shows
"Waiting for OSC...", and fills in its input when a message arrives; the unlink button cancels.
