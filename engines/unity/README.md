# Mecojoni for Unity

Runs Mecojoni grammars inside Unity through the same engine-agnostic C ABI
the Godot and Defold integrations use. The runtime "VM" is `mecojoni-core`
itself — the portable `no_std` compiler and runtime crate. Unity needs no
C++ adapter: C# P/Invokes the C ABI directly.

```
mecojoni-core (no_std Rust)          crates/mecojoni-core
        │
mecojoni-ffi (Rust, C ABI)           engines/godot/mecojoni-ffi  → libmecojoni_ffi.dylib/.so/.dll
        │  include/mecojoni.h
C# P/Invoke wrapper                  engines/unity/demo/Assets/Mecojoni/Runtime
        │
Unity (namespace Mecojoni: MecoGrammar, MecoPackageBuilder, MecoValue)
```

Both `.meco` source and compiled `.mecob` `bytecode/1` artifacts compile at
runtime; generation is deterministic (same seed, same artifact, same text —
see `../../COMPATIBILITY.md`).

## Layout

| Path | Contents |
| --- | --- |
| `demo/Assets/Mecojoni/` | The reusable integration — **copy this folder into any Unity project** |
| `demo/Assets/Mecojoni/Runtime/MecojoniNative.cs` | Raw P/Invoke surface mirroring `mecojoni.h` |
| `demo/Assets/Mecojoni/Runtime/Mecojoni.cs` | The managed API (no UnityEngine dependency) |
| `demo/Assets/Mecojoni/Plugins/<platform>/` | Native library, staged by `build.sh` (gitignored) |
| `demo/Assets/MecojoniDemo*.cs` | The interactive demo and its shared batch runner |
| `demo/Assets/Editor/MecojoniBatch.cs` | Headless verification entry point |
| `demo/Assets/StreamingAssets/` | The demo's `.meco` grammars |
| `build.sh` | Builds the FFI dynamic library and stages it into `Plugins/` |

## Building and testing

Requirements: the Rust workspace toolchain and Unity (any recent LTS; the
demo uses only IMGUI and StreamingAssets, no packages).

1. Stage the native library for your host platform:

   ```sh
   engines/unity/build.sh
   ```

2. Open `engines/unity/demo` in Unity and press **Play**. Note that Unity
   projects have no project *file*: the `demo` folder itself is the project.
   In Unity Hub use **Add → Add project from disk** and select the
   `engines/unity/demo` folder (don't browse inside it looking for a file
   to open — Hub validates it by `ProjectSettings/ProjectVersion.txt`).
   Alternatively, launch it directly:

   ```sh
   open -a "/Applications/Unity/Hub/Editor/<version>/Unity.app" \
       --args -projectPath "$(pwd)/engines/unity/demo"
   ```

   The demo spawns itself into the scene — no scene asset or prefab
   wiring — and shows:

   ```
   Mecojoni versions: ffi_abi=1 core_api=1 bytecode=bytecode/1
   entries: hello.greeting | default: hello.greeting
   bytecode/1 artifact round-trip OK (1245 bytes), match=true
   corrupt artifact rejected: E_BYTECODE_MAGIC
   hello seed 0 -> Welcome back, traveller.
   npc seed 0 -> Ripley, the market is unusually quiet.
   ```

   Click **Generate greeting** / **Generate NPC line** (or press SPACE) for
   fresh lines, and edit the **playerName** field to change the typed input.
   Everything is mirrored to the Console.

If Unity was already open when you ran `build.sh`, let it refresh (focus the
editor) so the plugin imports before entering Play mode.

### Headless / command-line verification

The same batch the demo prints can run without graphics or play mode, which
is how this integration is verified:

```sh
engines/unity/build.sh
"<path-to>/Unity" -batchmode -nographics \
    -projectPath engines/unity/demo \
    -executeMethod MecojoniBatch.Run -logFile -
```

Exits 0 after printing `MECOJONI UNITY BATCH OK`, or 1 with the failure.

## C# API

Everything lives in the `Mecojoni` namespace.

```csharp
using Mecojoni;

// Compile a single-module source, or load a compiled .mecob artifact.
// Both throw MecoException (stable E_* code + diagnostics) on failure.
MecoGrammar grammar = MecoGrammar.Compile("hello.meco", sourceText);
MecoGrammar shipped = MecoGrammar.LoadArtifact(File.ReadAllBytes(path));

MecoGenerated result = grammar.Generate(seed);
if (result.Ok)
    Debug.Log(result.Text);   // also result.Entry, result.Expansions,
                              //      result.SamplerWords
else
    Debug.Log(result.ErrorCode + ": " + result.ErrorMessage);
```

`Generate(seed, entry, data)`: `entry` null/`""` selects the package's
default entry; `data` maps declared `inputs:` names to typed values:

| C# value | Mecojoni type |
| --- | --- |
| `string` (or `MecoValue.Text`) | `text` |
| `MecoValue.EnumMember("member")` | enum member |
| `bool` | `boolean` |
| `int` / `long` | `number` (exact) |
| `MecoValue.Rational(n, d)` | `number` (exact rational) |

There is deliberately no conversion from `float`/`double`: generation is
exact and replayable, so fractional numbers must be spelled as rationals.

```csharp
var data = new Dictionary<string, MecoValue> { { "playerName", "Ripley" } };
grammar.Generate(3, null, data);
```

Introspection: `Entries`, `DefaultEntry`, `RuleCount`, `ProductionCount`,
`Warnings`, and static `MecoVersions.{FfiAbi, CoreApi, Bytecode}`. Tooling
can re-encode with `EncodeArtifact(profile)` (0 full, 1 mapped, 2 stripped).

Multi-module packages mirror the core's I/O-free contract — supply every
module and resolve every authored import edge explicitly:

```csharp
using (var builder = new MecoPackageBuilder())
{
    builder.AddModule("root",   "root.meco",   rootSource);
    builder.AddModule("common", "common.meco", commonSource);
    builder.ResolveImport("root", "./common.meco", "common");
    grammar = builder.Compile();   // throws MecoException on failure
}
```

## Notes

- **Shipping content:** precompile grammars with the authoring CLI
  (`meco compile-artifact game.meco --write game.mecob`), put the `.mecob`
  in `StreamingAssets/` (or any byte source), and load it with
  `MecoGrammar.LoadArtifact`. Artifacts are decoded under the format's
  hostile-input limits — corrupt input throws, never crashes.
- **Lifetime:** `MecoGrammar` and `MecoPackageBuilder` own native memory —
  `Dispose()` them (finalizers are the backstop). One instance must not be
  used from two threads at once; distinct instances are independent.
- **Platforms:** `build.sh` stages the host library. For other targets,
  build `mecojoni-ffi` with the matching Rust target and copy the result:

  | Target | Rust artifact | Plugins folder |
  | --- | --- | --- |
  | macOS | `libmecojoni_ffi.dylib` | `Plugins/macOS/` |
  | Linux | `libmecojoni_ffi.so` | `Plugins/Linux/x86_64/` |
  | Windows | `mecojoni_ffi.dll` | `Plugins/Windows/x86_64/` |

  The wrapper is plain P/Invoke over blittable types, so it works on both
  Mono and IL2CPP scripting backends.
- Entries that produce complete localized messages need a host formatter,
  which this wrapper does not expose yet; such entries report
  `E_FORMATTER_REQUIRED`. Sessions (`diverse/1`), repetition stores, and
  snapshots are likewise core features not yet surfaced here.
