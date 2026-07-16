# Mecojoni release gate

The implementation is feature-complete against `ROADMAP.md`. Before an actual
distribution, run the commands in `CONFORMANCE.md` and verify:

- source, bytecode, sampler, ABI, snapshot, CLI, diagnostic, and workload versions match
  `COMPATIBILITY.md`;
- every filesystem, subprocess, Deno, and browser test passes;
- the `thumbv6m-none-eabi` core and release WASM artifact build;
- generated Rust API docs contain no warnings;
- `operations.contract` has no unexplained drift;
- `BYTECODE_FORMAT.md` matches golden artifacts and the exact runtime fingerprint;
- native benchmark changes include before/after evidence and WASM has zero live
  ABI allocations/handles after disposal;
- README, specifications, interface contract, conformance, and
  benchmark documents agree.

The repository intentionally does not invent a license. Cargo publication remains
disabled until the owner adds the intended root license and selects package
distribution versions.
