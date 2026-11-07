# Agent access (Prism MCP)

Prism can expose a localhost MCP server so Cursor, Claude Code, or other agents can drive the open mixer. Enable it in **Run → Agent Access** (off at every launch).

## Connect

**Cursor / Claude (this session):** copy the snippet from Agent Access after enabling. The bearer token rotates each session.

**One-time stdio attach** (no token in `mcp.json`):

```json
{
  "mcpServers": {
    "prism": {
      "command": "/path/to/prism",
      "args": ["--mcp-stdio"]
    }
  }
}
```

`prism --mcp-stdio` attaches to a running mixer with Agent access enabled.

## Workflow

1. **`catalog`** — toolboxes, per-op “when” hints, endpoints, units, limitations (no schemas).
2. **`toolbox({name})`** — full JSON schemas for ops in that toolbox.
3. **`apply({ops:[{tool, args}, …]})`** — run mutations in order.
4. **`inspect({clips:true, detail:true, graph:true})`** — mixer state, clip ids, graph wiring, decks, transition progress, recording.
5. **`capture()`** — JPEG still of program output (use to verify a take).

Homepage endpoint: `POST /mcp` with `Authorization: Bearer <token>`.

Pinned endpoints (`/mcp/sources`, `/mcp/decks`, `/mcp/transition`, `/mcp/panic`, `/mcp/output`, `/mcp/session`, `/mcp/graph`) list that toolbox’s ops directly. `catalog`, `toolbox`, and `apply` are only on `/mcp`; `inspect` and `capture` work on both. Toolbox ops can also be called by name directly on `/mcp` instead of through `apply`.

`apply` takes **toolbox ops only**. `catalog`, `toolbox`, `inspect`, `capture`, and `apply` itself return `unknown_op` inside an `ops` array; call them directly.

## Conventions

| Topic | Rule |
|-------|------|
| Time | Seconds |
| Decks | Two fixed decks: **Staging** (next up, shown left) and **Live** (on air, shown right). Transition progress (`fader` in `inspect`) runs 0 = Live to 100 = Staging. A `take` animates it to 100, then the Staging content becomes Live, the former Live content moves to Staging, and progress resets to 0 |
| Scenes | The show is a list of scenes, each with its own graph. `list_scenes` shows them; `stage_scene` puts one on Staging (by id or 1-based position); `enter_scene` selects the scene that clip and graph ops act on. `add_scene` appends a scene; `take_scene` stages and takes in one step; `remove_scene`, `reorder_scene` (1-based `to`), `duplicate_scene`, `exit_scene` manage the list; `set_scene_props` sets name, color, notes, `transition` ({mode, durationMs} or null for the show default), `endAction` (hold, loop, autoAdvance, black), `autoAdvanceMs`, `advanceOnMediaEnd`, `hotkey`; `set_default_transition` sets the show default; `preflight` lists missing files and devices across all scenes |
| Clip reference | `clip` id from `inspect({clips:true})` (the edited scene). Required — clip ops never fall back to the current deck assignment |
| Adding sources | `add_clip` / `add_source` only put a node on the edited scene's graph. Wire it to Output with `connect`, then `stage_scene` the scene |
| Process / graph | `add_process_node` / `add_layer_node` only place nodes. Call `connect` to wire. `list_nodes` or `inspect({graph:true})` for ids |
| Multi-input effects | Composite effects (`blend`, `mask`, `mix`, `displace_image`, `switch`) have extra input ports after the main `in`. `connect` takes `to_port` as a slot number (`0` = main, `1`.. = extras) or the port name (`"b"`, `"mask"`, `"map"`, `"in1"`..`"in3"`); `list_nodes` shows a process node's `inputs` names. An unconnected extra input reads as transparent. `lut` takes a `file` param: set `params.lut` to an absolute `.cube` or LUT PNG path |
| Modulators | `add_modulator_node({kind, params})` places a Modulator (`lfo`, `envelope`, `map_range`, `smooth`, `random`, `trigger`, `counter`, `toggle`, `math`); `set_modulator_params` edits it. `bind_param({node, param, modulator})` drives a numeric parameter with its output (about 30 Hz; omit `modulator` to unbind). `param` is the expression key: `"opacity"` or `"params.opacity"` on a process node, `"volume"` on others. `list_nodes` and `inspect({graph:true})` list modulators as `{type:"modulator", kind, params, value}` with the live `value`, and show each node's `expressions` (a bound parameter reads `mod(<id>)`). Modulator inputs (`in`, `b`) take `"low"`, `"mid"`, `"high"`, `"level"`, `"beat"` (Audio Analyzer bands), `"<analyzerId>:<band>"`, `"mod:<id>"` or a number. LFO / random timing is `rateHz`, or with `sync:true` `rateBeats` of the global BPM clock (0.25 = 1/16 note … 32 = 8 bars). These ops are not undoable |
| Take | `stage_scene`, `capture()` to check, then `take` (animated) or `cut` (instant) |
| Per-deck ops | `play`, `pause`, and `seek` take `deck: "live"` or `"staging"` |
| Atomicity | `apply` is **not** atomic: on failure the ops before it stay applied. Check `stopped` / `failed` / `done` |
| Undo | None. Live takes are immediate |
| Errors | `{ok:false, error:<code>, detail:<text>}` — `bad_args`, `not_found`, `type_mismatch`, `unknown_op`, `unknown_toolbox`, `wrong_endpoint`, `wrong_toolbox`, `apply_failed`, `capture_failed`, `conflict` |
| Change detection | Every `inspect` includes `revision`; pass `since:<revision>` to get `{unchanged:true}` when state is current |

## Toolbox reference

| Toolbox | When to use |
|---------|-------------|
| `sources` | Add/list/rename clips; set clip speed; text/SVG template/shader updates; scene sources (one scene inside another); cameras and NDI |
| `decks` | List/add/enter/stage/take/edit/reorder scenes, preflight, play/pause/seek per deck |
| `transition` | `set_transition_progress`, `cut`, `take`, look and duration |
| `panic` | Blackout, freeze, stay-tuned slate |
| `output` | Record program, NDI, virtual camera |
| `session` | Save/load a `.psm` session |
| `graph` | Process/layer nodes and wiring on the blue video chain |

## Example

Add a scene with a file, wire it to the Output node, and stage it:

```json
{
  "ops": [
    {"tool": "add_scene", "args": {"name": "Intro"}},
    {"tool": "enter_scene", "args": {"scene": "<id from previous result>"}},
    {"tool": "add_clip", "args": {"path": "/path/to/clip.mp4"}}
  ]
}
```

`apply` cannot reference an id produced earlier in the same batch. Add first, read `done[n].result.id`, `list_nodes` for the Output id, `connect({from:<clip>, to:<output>})`, then stage and take:

```json
{
  "ops": [
    {"tool": "stage_scene", "args": {"scene": "<id>"}},
    {"tool": "take", "args": {}}
  ]
}
```

Then verify visually — `capture` is a homepage tool and returns `unknown_op` inside `ops`, so call it on its own:

```json
{"name": "capture", "arguments": {}}
```

Shader plus a Crop process, wired to Output:

```json
{"name": "add_source", "arguments": {"kind": "shader"}}
```

Then, with the returned input id and `list_nodes` output id:

```json
{
  "ops": [
    {"tool": "add_process_node", "args": {"effect": "crop", "params": {"x": 0.1, "w": 0.8}}},
    {"tool": "connect", "args": {"from": "<shader id>", "to": "<crop id>"}},
    {"tool": "connect", "args": {"from": "<crop id>", "to": "<output id>"}}
  ]
}
```

`apply` cannot use an id produced earlier in the same batch for later ops — add the process node first, read `id`, then connect.

## Security

- Binds to `127.0.0.1` only.
- Any local process with the session token has full mixer access.
- Turn Agent access off when finished.
