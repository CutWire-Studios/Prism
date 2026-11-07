# effects_golden.json

Data for `goldenPixels` in `tests/test_shadereffects.cpp`. Top level is an array of cases; add a case by appending an object (no C++ changes).

- `slug`: effect slug from `effects.json`.
- `input`: `{"type":"solid","color":"#rrggbbaa"}` or `{"type":"hgradient","from":"#..","to":"#.."}` (left to right). Colors are straight, non-premultiplied RGBA.
- Hex colors are CSS order, `#rrggbb` or `#rrggbbaa` (alpha last), parsed by `ShaderEffects::parseHexColor`. Colour defaults in `effects.json` use the same format.
- `size`: `[w,h]`, default `[64,64]`. `params`: merged over the spec defaults. `time`: reserved (default 0).
- `samples`: list of `{"uv":[x,y], "rgba":[r,g,b,a], "tol":3}`; `rgba` is 0-255, `tol` is the max per-channel difference (default 3).
- uv (0,0) is the top-left of the image as seen; pixel is `x = uv.x*(w-1)`, `y = uv.y*(h-1)` (rounded).
- Output is straight alpha (shaders do not premultiply); e.g. invert of `#ff000080` is `[0,255,255,128]`.
- Keys starting with `_` (e.g. `_comment`) are ignored.
- `inputs` (optional): `{"<name>": {input spec}}` feeds the effect's extra input ports, named as in the spec's `inputs` array in `effects.json` (e.g. `"b"` for blend, `"in1"` for switch). Each uses the same input spec as `input`, at the case's `size`. A name left out stays unconnected, i.e. a transparent texture.
- String values in `params` have `$DATA` replaced by this directory, so a `file` param can point at `$DATA/identity2.cube`.
