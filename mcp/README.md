# Mecojoni MCP server

An [MCP](https://modelcontextprotocol.io) server that exposes the `meco` CLI as tools for any
MCP-compatible agent. It is a thin process wrapper — every tool below builds an argument list for
the real `meco` binary, runs it with `--output jsonl`, and relays the result. No grammar or compiler
logic lives here; `mecojoni-core` stays the single source of truth, and the server is just another
host, like the CLI or the WASM bridge.

## Prerequisites

Build the CLI once before using the server:

```sh
cargo +1.85.0 build -p mecojoni-cli --release
```

The server looks for the binary at `target/release/meco` (falling back to `target/debug/meco`)
relative to the repository root, or at the path in `MECO_BIN` if you set it.

[Deno](https://deno.com) 2.x is required to run `mcp/server.ts` itself — no `npm install` step, no
`package.json`; dependencies are fetched from `npm:` specifiers on first run, matching how `js/`
already works in this repository.

## Tools

Each tool maps to one `meco` subcommand. `source` and `artifactPath` are resolved against
`MECO_PROJECT_ROOT` (or this repository, by default), so the server behaves the same regardless of
the working directory the MCP client launches it from. Every resolved path — relative or absolute —
must stay within that root; a `..`-escape or an absolute path pointing elsewhere on the host is
rejected, since a tool argument here may itself relay untrusted content.

| Tool                     | `meco` command      | Notable arguments                                   |
| ------------------------ | ------------------- | --------------------------------------------------- |
| `meco_check`             | `check`             | `source`, `denyWarnings`, `messages`                |
| `meco_generate`          | `generate`          | `source`, `seed`, `count`, `entry`, `data`          |
| `meco_trace`             | `trace`             | `source`, `seed`, `count`, `entry`, `data`          |
| `meco_lint`              | `lint`              | `source`, `denyWarnings`                            |
| `meco_audit`             | `audit`             | `source`, `samples`, `seed`, `entry`, `data`        |
| `meco_manifest`          | `manifest`          | `source`, `messages`                                |
| `meco_fmt`               | `fmt`               | `source`, `write`                                   |
| `meco_bench`             | `bench`             | `source`, `count`, `seed`, `entry`, `data`          |
| `meco_compile_artifact`  | `compile-artifact`  | `source`, `write` (required), `messages`, `profile` |
| `meco_inspect_artifact`  | `inspect-artifact`  | `artifactPath`                                      |
| `meco_verify_artifact`   | `verify-artifact`   | `artifactPath`                                      |
| `meco_generate_artifact` | `generate-artifact` | `artifactPath`, `seed`, `count`, `entry`, `data`    |

`seed` accepts a number or a numeric string (`u64` can exceed the range a JS `number` safely
represents). `data` is a `{ name: value }` object of typed host inputs declared in the grammar's
`inputs:` front matter, expanded into repeated `--data name=value` flags.

Every tool returns one text content block containing `{ exitCode, results,
stderr? }`, where
`results` is the parsed `--output jsonl` array. `meco` only writes to stdout on success, so a tool
only reports `isError: true` when stdout was empty and the process exited nonzero — the CLI's own
diagnostic becomes the error message. A nonzero exit code alongside real output (e.g.
`check
--denyWarnings` succeeding but flagging warnings) is reported inside the JSON body, not as a
tool error.

## Configuring an MCP client

Every configuration below runs:

```sh
deno run --allow-read --allow-run --allow-env=MECO_BIN,MECO_PROJECT_ROOT /absolute/path/to/mecojoni/mcp/server.ts
```

Scope `--allow-run` to the exact binary path (e.g.
`--allow-run=/absolute/path/to/mecojoni/target/release/meco`) if your client supports arbitrary Deno
flags and you want the tightest possible permissions.

### Claude Code

```sh
claude mcp add mecojoni -- deno run --allow-read --allow-run --allow-env=MECO_BIN,MECO_PROJECT_ROOT /absolute/path/to/mecojoni/mcp/server.ts
```

### Claude Desktop

Add to `claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "mecojoni": {
      "command": "deno",
      "args": [
        "run",
        "--allow-read",
        "--allow-run",
        "--allow-env=MECO_BIN,MECO_PROJECT_ROOT",
        "/absolute/path/to/mecojoni/mcp/server.ts"
      ]
    }
  }
}
```

### Cursor, Windsurf, Cline, and other clients using the same `mcpServers` shape

Most MCP clients (Cursor's `.cursor/mcp.json`, Windsurf's `mcp_config.json`, Cline's MCP settings,
VS Code's `mcp.json`) accept the same `command`/`args` object shown above for Claude Desktop — copy
the `mecojoni` entry into whichever file that client reads.

## Development

```sh
deno task mcp:check   # deno fmt --check + deno check
deno task mcp:test    # builds the CLI, then runs mcp/server_test.ts
```

`server_test.ts` calls each tool's registered handler directly against the real fixtures in
`examples/`, and separately, this was verified end-to-end with a real `@modelcontextprotocol/sdk`
`Client` connected over `StdioClientTransport` to confirm the full initialize → `tools/list` →
`tools/call` round trip works, not just the internal handler functions.

## Why a process wrapper instead of linking `mecojoni-core` directly

`mecojoni-core` is a dependency-free, `no_std` Rust crate; a native Rust MCP server (using the
`rmcp` SDK) is a reasonable future alternative if this becomes a permanent part of the project,
avoiding subprocess overhead per call. This TypeScript wrapper was chosen first because it reuses
the CLI's existing, tested `--output jsonl` contract exactly as-is, and needs no new Rust
dependencies (the official MCP TypeScript SDK is a real dependency of `mcp/`, but it's isolated from
`mecojoni-core`'s own zero-dependency guarantee — the same boundary `mecojoni-cli` and
`mecojoni-wasm` already draw).
