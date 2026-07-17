# Mecojoni for Defold

Runs Mecojoni grammars inside Defold through a native extension. The runtime
"VM" is `mecojoni-core` itself — the portable `no_std` compiler and runtime
crate — reached through the same engine-agnostic C ABI the Godot integration
uses, wrapped in a thin Lua bridge:

```
mecojoni-core (no_std Rust)          crates/mecojoni-core
        │
mecojoni-ffi (Rust, C ABI)           engines/godot/mecojoni-ffi  → libmecojoni_ffi.a
        │  include/mecojoni.h
Defold native extension (C++/Lua)    engines/defold/demo/mecojoni
        │
Defold (Lua module: mecojoni)
```

Both `.meco` source and compiled `.mecob` `bytecode/1` artifacts compile at
runtime; generation is deterministic (same seed, same artifact, same text —
see `../../COMPATIBILITY.md`).

## Layout

| Path | Contents |
| --- | --- |
| `demo/mecojoni/` | The reusable native extension — **copy this folder into any Defold project** |
| `demo/mecojoni/ext.manifest` | Defold extension manifest |
| `demo/mecojoni/src/mecojoni_ext.cpp` | The C++ bridge from the C ABI to the Lua `mecojoni` module |
| `demo/mecojoni/include/mecojoni.h` | The C ABI contract (refreshed by `build.sh`) |
| `demo/mecojoni/lib/<platform>/` | Prebuilt `libmecojoni_ffi.a`, staged by `build.sh` (gitignored) |
| `demo/main/` | The demo bootstrap collection, script, and `.meco` data |
| `build.sh` | Builds the FFI static library and stages the header + lib into the extension |

## Building and testing

Requirements: the Rust workspace toolchain, plus Defold (the editor bundles
everything else). Native extensions are compiled by Defold's build server, so
building needs network access to it.

1. Stage the C ABI into the extension (builds `mecojoni-ffi` and copies the
   header + static library for your host platform):

   ```sh
   engines/defold/build.sh
   ```

   Pass a Defold platform id to cross-stage, e.g. `engines/defold/build.sh arm64-osx`.

2. Open `engines/defold/demo` in the Defold editor and press **Build → Build
   and Run** (or ⌘/Ctrl-B). The extension is compiled on the build server and
   linked into the engine automatically.

The demo prints to the editor **Console** panel:

```
Mecojoni versions: ffi_abi=1 core_api=1 bytecode=bytecode/1
entries: hello.greeting | default: hello.greeting
bytecode/1 artifact round-trip OK (1245 bytes), match=true
hello seed 0 -> Welcome back, traveller.
npc seed 0 -> Ripley, the market is unusually quiet.
MECOJONI DEMO DONE -- press SPACE for more, ESC to quit
```

The window stays open: press **SPACE** (or ENTER) to generate fresh random
lines, **ESC** to quit. Output goes to the console — Defold GUI text needs a
font asset, so the demo keeps its moving parts minimal and prints instead.

### Headless / command-line build

To build and run without the editor (what this integration was verified with),
using the Defold command-line tool `bob.jar` and its bundled JRE:

```sh
cd engines/defold/demo
java -jar bob.jar \
  --platform arm64-macos --architectures arm64-macos \
  --variant headless --build-server https://build.defold.com \
  build
./build/arm64-osx/dmengine        # prints the batch, then waits for input
```

Note the platform/architecture argument uses `arm64-macos` while the produced
build and extension `lib/` folders use Defold's legacy `arm64-osx` name.

## Lua API

The extension registers a global `mecojoni` module.

```lua
-- Compile a single-module source (bytes, e.g. from sys.load_resource):
local grammar, err = mecojoni.compile("hello.meco", source_string)
if not grammar then print(err.error_code, err.error_message) end

-- Or load a compiled .mecob artifact:
local grammar = mecojoni.load_artifact(bytes_string)
```

`mecojoni.compile` / `mecojoni.load_artifact` return the grammar on success, or
`nil` plus an `{error_code, error_message}` table on failure. Artifacts are
decoded under the format's hostile-input limits — a corrupt or over-budget
`.mecob` is rejected with a stable `E_*` code and no partial grammar.

```lua
local result = grammar:generate(seed, entry, data)
if result.ok then
    print(result.text)   -- also result.entry, result.expansions, result.sampler_words
else
    print(result.error_code, result.error_message)
end
```

`seed` is a number (use whole values; determinism is exact). `entry` may be
`""` to use the package's default entry. `data` is an optional table mapping
declared `inputs:` names to values:

| Lua value | Mecojoni type |
| --- | --- |
| string | `text` |
| `{enum = "member"}` or `mecojoni.enum("member")` | enum member |
| boolean | `boolean` |
| integer number | `number` (exact) |
| `{numerator, denominator}` | `number` (exact rational) |

Non-integer numbers are rejected on purpose: generation is exact and
replayable, so fractional values must be spelled as rationals.

Introspection: `grammar:entries()`, `grammar:default_entry()`,
`grammar:rule_count()`, `grammar:production_count()`, `grammar:warnings()`.
Tooling can re-encode with `grammar:encode_artifact(profile)` (0 full, 1
mapped, 2 stripped), returning the bytes as a Lua string.

### Multi-module packages

Mirrors the core's I/O-free contract: supply every module and resolve every
authored import edge explicitly.

```lua
local builder = mecojoni.package_builder()
builder:add_module("root",   "root.meco",   sys.load_resource("/main/data/npc/root.meco"))
builder:add_module("common", "common.meco", sys.load_resource("/main/data/npc/common.meco"))
builder:resolve_import("root", "./common.meco", "common")
local grammar = builder:compile()   -- nil + error table on failure
```

## Notes

- **Shipping content:** precompile grammars with the authoring CLI
  (`meco compile-artifact game.meco --write game.mecob`), bundle the `.mecob`
  as a custom resource, and load it with
  `mecojoni.load_artifact(sys.load_resource("/path/game.mecob"))`.
- **Handles** (grammars, package builders) are Lua userdata freed by their
  `__gc`; one handle must not be used from two threads at once.
- **Platforms:** `build.sh` stages a static library for the host. For other
  targets, build `mecojoni-ffi` for that Rust target and place the `.a` in
  `demo/mecojoni/lib/<defold-platform>/` (e.g. `x86_64-linux`, `arm64-osx`).
- Entries that produce complete localized messages need a host formatter,
  which this bridge does not expose yet; such entries report
  `E_FORMATTER_REQUIRED`. Sessions (`diverse/1`), repetition stores, and
  snapshots are likewise core features not yet surfaced here.
