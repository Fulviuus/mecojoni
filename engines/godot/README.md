# Mecojoni for Godot

Runs Mecojoni grammars inside Godot 4 through a native extension. The
runtime "VM" is `mecojoni-core` itself — the portable `no_std` compiler and
runtime crate — reached through two thin adapters:

```
mecojoni-core (no_std Rust)          crates/mecojoni-core
        │
mecojoni-ffi (Rust, C ABI)           engines/godot/mecojoni-ffi  → libmecojoni_ffi.a
        │  include/mecojoni.h
GDExtension adapter (C++)            engines/godot/gdextension   → libmecojoni.<platform>.<target>
        │
Godot (GDScript: MecoGrammar, MecoPackageBuilder)
```

Both compiled `.mecob` `bytecode/1` artifacts and `.meco` source compile at
runtime; generation is deterministic (same seed, same artifact, same text —
see `COMPATIBILITY.md`).

## Layout

| Path | Contents |
| --- | --- |
| `mecojoni-ffi/` | Rust crate exposing the C ABI (`staticlib`/`cdylib`); part of the workspace |
| `mecojoni-ffi/include/mecojoni.h` | The authoritative C contract, usable from any C/C++ host |
| `mecojoni-ffi/tests/c_smoke.c` | Standalone C test proving header and library agree |
| `gdextension/` | C++ GDExtension adapter and its CMake build |
| `demo/` | Minimal Godot 4.4 project exercising the extension |

## Building

Requirements: Rust (workspace toolchain), CMake ≥ 3.22, a C++17 compiler,
and a `godot-cpp` checkout:

```sh
cd engines/godot/gdextension
git clone --depth 1 --branch godot-4.4-stable https://github.com/godotengine/godot-cpp
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target mecojoni.template_debug   # editor + debug builds
cmake --build build --target mecojoni.template_release # exported release builds
```

CMake drives `cargo build -p mecojoni-ffi --release` automatically and links
the static library into the extension, so each platform ships one shared
library. Outputs land in `demo/bin/`, where `mecojoni.gdextension` expects
them; copy `demo/bin/` (the `.gdextension` file plus libraries) into any
other Godot project to use the extension there.

To try the demo, open `engines/godot/demo` in Godot 4.4+ and run it. It compiles
`data/hello.meco`, round-trips an in-memory artifact, and compiles the
multi-module `data/npc` package with a host input:

```
hello seed 0 -> Welcome back, traveller.
npc seed 0 -> Ripley, the market is unusually quiet.
```

## GDScript API

### MecoGrammar (RefCounted)

```gdscript
var grammar := MecoGrammar.new()

# Load one of:
grammar.compile_source("hello.meco", source_text)    # single-module source
grammar.compile_file("res://data/hello.meco")
grammar.load_artifact(bytes)                         # compiled .mecob bytes
grammar.load_artifact_file("res://data/hello.mecob")
```

Each loader returns a Godot `Error`; on failure `get_error_code()` holds the
stable diagnostic code (for example `E_BYTECODE_CORRUPT`) and
`get_error_message()` one rendered line per diagnostic. Artifacts are decoded
under the format's hostile-input limits — a corrupt or over-budget `.mecob`
is rejected with no partial grammar.

```gdscript
var result := grammar.generate(seed, "hello.greeting", {"playerName": "Ripley"})
if result.ok:
    print(result.text)       # also: result.entry, result.expansions,
                              #       result.sampler_words
else:
    print(result.error_code, ": ", result.error_message)
```

`entry` may be `""` to use the package's default entry. The `data`
dictionary supplies declared `inputs:`; value types map onto the typed
schema:

| GDScript value | Mecojoni type |
| --- | --- |
| `String` | `text` |
| `StringName` (`&"member"`) | enum member |
| `bool` | `boolean` |
| `int` | `number` (exact) |
| `[numerator, denominator]` | `number` (exact rational) |

Floats are rejected on purpose: generation is exact and replayable, so
fractional numbers must be spelled as rationals.

Introspection: `get_entries()`, `get_default_entry()`, `get_rule_count()`,
`get_production_count()`, `get_warnings()`, `is_compiled()`, and static
`MecoGrammar.versions()`. Tooling can also re-encode with
`encode_artifact(profile)` (0 full, 1 mapped, 2 stripped).

### MecoPackageBuilder (RefCounted)

Multi-module packages mirror the core's I/O-free contract: the host supplies
every module and resolves every authored import edge explicitly.

```gdscript
var builder := MecoPackageBuilder.new()
builder.add_module_file("root", "res://data/npc/root.meco")
builder.add_module_file("common", "res://data/npc/common.meco")
builder.resolve_import("root", "./common.meco", "common")
var grammar := builder.compile()     # null on failure; see get_error_message()
```

## Notes

- **Shipping content:** precompile grammars with the authoring CLI
  (`meco compile-artifact game.meco --write game.mecob`) and load the
  `.mecob` at runtime; source compilation in exported games is optional.
  Add `*.meco,*.mecob` to your export preset's non-resource include filter
  so raw files are packed.
- **Threading:** instances are independent; one instance must not be used
  from two threads at once.
- **Entries that produce complete localized messages** need a host
  formatter, which this adapter does not expose yet; such entries report
  `E_FORMATTER_REQUIRED`. Sessions (`diverse/1`), repetition stores, and
  snapshots are likewise core features not yet surfaced here.
- The C ABI is independently usable: link `libmecojoni_ffi` and include
  `mecojoni-ffi/include/mecojoni.h` from any engine or tool. See
  `mecojoni-ffi/tests/c_smoke.c` for a complete example.
