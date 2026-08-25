# Agent access (Prism MCP)

Prism can expose a localhost MCP server so Cursor, Claude Code, or other agents can drive the open mixer. Enable it in **Run → Agent Access** (off at every launch).

## Connect

**Cursor / Claude (this session):** copy the snippet from Agent Access after enabling. The bearer token rotates each session.

**One-time stdio attach** (no token in `mcp.json`):

```json
{
  "mcpServers": {
    "prism": {
      "command": "/path/to/Prism",
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
4. **`inspect({clips:true, detail:true})`** — mixer state, clip ids, decks, fader, recording.
5. **`capture()`** — JPEG still of program output (use to verify a take).

Homepage endpoint: `POST /mcp` with `Authorization: Bearer <token>`.

Pinned endpoints (`/mcp/sources`, `/mcp/decks`, `/mcp/transition`, `/mcp/panic`, `/mcp/output`, `/mcp/session`) list that toolbox’s ops directly. `catalog`, `toolbox`, and `apply` are only on `/mcp`; `inspect` and `capture` work on both. Toolbox ops can also be called by name directly on `/mcp` instead of through `apply`.

`apply` takes **toolbox ops only**. `catalog`, `toolbox`, `inspect`, `capture`, and `apply` itself return `unknown_op` inside an `ops` array; call them directly.

## Conventions

| Topic | Rule |
|-------|------|
| Time | Seconds |
| Fader | 0 = deck A, 100 = deck B |
| Clip reference | `clip` id from `inspect({clips:true})`. Required — clip ops never fall back to the current deck assignment |
| Adding sources | `add_clip` / `add_source` only put a node on the graph. Call `select_a` or `select_b` to put it on a deck |
| Take | Assign the off-air deck, `capture()` to check, then `cut` or `auto_transition` |
| Atomicity | `apply` is **not** atomic: on failure the ops before it stay applied. Check `stopped` / `failed` / `done` |
| Undo | None. Live takes are immediate |
| Errors | `{ok:false, error:<code>, detail:<text>}` — `bad_args`, `not_found`, `type_mismatch`, `unknown_op`, `unknown_toolbox`, `wrong_endpoint`, `wrong_toolbox`, `apply_failed`, `capture_failed`, `conflict` |
| Change detection | Every `inspect` includes `revision`; pass `since:<revision>` to get `{unchanged:true}` when state is current |

## Toolbox reference

| Toolbox | When to use |
|---------|-------------|
| `sources` | Add/list/rename clips; text/HTML/shader updates; cameras and NDI |
| `decks` | Assign A/B, play/pause/seek, speed |
| `transition` | T-bar, CUT, AUTO, look and duration |
| `panic` | Blackout, freeze, stay-tuned slate |
| `output` | Record program, NDI, virtual camera |
| `session` | Save/load a `.psm` session |

## Example

Import a file and take it to program:

```json
{
  "ops": [
    {"tool": "add_clip", "args": {"path": "/path/to/clip.mp4"}},
    {"tool": "select_b", "args": {"clip": "<id from previous result>"}}
  ]
}
```

`apply` cannot reference an id produced earlier in the same batch. Add first, read `done[0].result.id`, then select:

```json
{
  "ops": [
    {"tool": "select_b", "args": {"clip": "<id>"}},
    {"tool": "set_fader", "args": {"value": 100}}
  ]
}
```

Then verify visually — `capture` is a homepage tool and returns `unknown_op` inside `ops`, so call it on its own:

```json
{"name": "capture", "arguments": {}}
```

## Security

- Binds to `127.0.0.1` only.
- Any local process with the session token has full mixer access.
- Turn Agent access off when finished.
