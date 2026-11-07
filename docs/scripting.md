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
| `prism.log(...)` | Same as `print`. |
| `prism.time()`, `prism.clock()`, `prism.date(fmt, t)` | The `os.time`, `os.clock` and `os.date` equivalents. |

Scripts can only reach nodes of their own scene's graph; other node ids raise
`node N is not in this scene`. Node ids are shown in the inspector header.

## Parameter expressions

Numeric inspector parameters with an "f" button can be driven by a Lua expression instead of
a fixed value. Click "f", type an expression and the slider is replaced by the expression
field; clicking "f" again removes it. Expressions are stored per node in
`params.expressions` (`{ "<param>": "<expr>" }`).

Expressions are evaluated at about 30 Hz while the scene's runtime is active. Available names:

- `t`: seconds since the scene's runtime was created
- `audio`: `{low, mid, high, level, beat}` (zeros without an analyzer)
- `param(nodeId, name)`: as `prism.param` (read only)
- everything in `math` through the global environment, for example `math.sin(t)`

The result is written as the stored parameter value (for example opacity 0..1 even though the
inspector shows percent) and is not clamped. A syntax or runtime error, a non-numeric result
or an instruction overrun is shown under the field and leaves the last value in place.
