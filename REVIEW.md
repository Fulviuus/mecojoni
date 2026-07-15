# Mecojoni — Review & Assessment

*Updated 2026-07-15 with second-opinion corrections, benchmark data, and additional findings.*

## 1. What It Does Well

### Grammar Design
- **Markdown-like readability is genuinely achieved.** `# rule`, `- production`, `@reference`, `[weight]` — anyone who's written Markdown can read a `.meco` file. This is non-trivial and the project's strongest asset.
- **Versioned from commit zero** (`@meco 1`). Many DSLs skip this and pay for it later.
- **`@empty`** as a keyboard-friendly empty production is thoughtful UX. Most people can't type ε.
- **`@@` escaping** is clean and conventional.
- **`//` comments** are natural and editor-agnostic.
- **Weights are optional and decimal-capable** — `[0.5]` works, `[3]` works, omission means `1`. Good ergonomics.
- **Separation of grammar from sampler** is architecturally sound. The grammar defines *what may be said*; the sampler picks *which derivation*. This is the right abstraction.

### Compiler / Validator
- **Thorough static analysis.** The `compile()` pass catches: missing/duplicate `@meco`/`@start`, invalid rule names, undefined `@references`, rules without productions, empty productions, invalid weights, productions outside rule sections, unknown directives, and — crucially — **reachable non-terminating rules** via fixed-point productivity analysis.
- **Filename + line number in every diagnostic.** Mostly — see [corrections below](#line-number-corrections).
- **Reachability from `@start`** means only relevant non-terminating rules are flagged, not dead rules.

### Generator / Sampler
- **The `varied` selection mode is the killer feature.** Three mechanisms: structural cooldown (recently-used productions are penalized), subtree diversity boosting (large branches aren't hidden behind small parent choices), and output novelty scoring (up to 12 candidates, pick the one with freshest openings/endings). This is more sophisticated than any competitor in this weight class.
- **Probability-sensitive exemptions** for recursive and empty-production rules are correctly reasoned — those weights encode termination/optionality strategies that must not be distorted.
- **Edge fragment tracking covers openings, endings, internal sentence boundaries, AND exact lines.** This is exactly right for dialogue, where humans notice repeated sentence starts/ends far more than repeated mid-sentence words.
- **Deterministic seeding** (FNV-1a + mulberry32) is solid and standard.

### Audit System
- **The repetition audit** generates a large sample, finds repeated fragments, then **replays the deterministic sequence** to attribute each fragment to its likely correlated derivation step. This is genuinely useful tooling — something most competitors don't have. (But see [attribution caveat](#audit-attribution) below.)

### Engineering Discipline
- **Zero dependencies.** Correct choice for an embeddable library.
- **ESM-only.** Forward-looking.
- **~1,436 lines total across all source + tests.** Impressively compact for what it does.
- **Reasonably commented internals.**

---

## 2. What It Does Poorly

### Grammar/Syntax Flaws

| # | Issue | Severity |
|---|-------|----------|
| 1 | **No escape for literal `[` at line start when it looks like a weight.** `[bracketed text]` at position 0 is fine (only `[<positive-decimal>]` is parsed as a weight), but `[3]` cannot start a production as literal text. The original review overstated this — general bracket text works; the hole is narrower. | **Low** — rarer than originally claimed |
| 2 | **Rule names can't start with digits.** `# 2nd-floor` is invalid. The regex `[A-Za-z][A-Za-z0-9_-]*` is ASCII-only and letter-start. Real taxonomies use numbered categories. Unicode rule names are also invalid — both declarations and references require ASCII. | **Low** |
| 3 | **No inline comments.** Acknowledged as a limitation, but `- screamed // only for horror` silently treats `// only for horror` as terminal text. This will bite authors. | **Medium** |
| 4 | **Whitespace semantics are invisible and brittle.** The parser trims trailing whitespace, ignores indentation before syntax, and consumes at most one space after `-` and at most one after a weight. Any additional space becomes output. The README's own optional-title example depends on double-spacing after `[1]` — a semantic landmine that is nearly invisible in review and easy for a formatter to destroy. There is no glue operator, whitespace normalization, or way to emit a newline. | **High** |
| 5 | **No quoting or character escapes in terminal text.** Every character is literal except `@`. No way to express tabs, newlines within productions, or special Unicode escape sequences. Since physical lines are syntax, the language cannot generate paragraph breaks or multi-line dialogue blocks. | **Medium** |
| 6 | **`@meco 1` version regex accepts `1.0.0` but only `1` is supported.** The `\d+(?:\.\d+)*` pattern is premature. Either support semver properly or just match `\d+`. | **Low** |
| 7 | **References need an explicit delimiter.** `@nounlike` is a reference to `nounlike`, not `@noun` followed by `like`. There is no braced form such as `@{noun}like`. This makes suffixes and morphology awkward precisely where a text grammar should be strongest. | **Medium** |
| 8 | **The grammar cannot express agreement, memory, or constraints.** Mecojoni is a plain context-free expansion system. No parameters, variables, captures, conditions, or transformations. Authors must encode constraints by duplicating rules for every valid combination — a cross-product maintenance problem for serious dialogue. This is the biggest gap between Mecojoni's stated large-dialogue ambition and its actual grammar model. | **High** |
| 9 | **No language specification.** The README is the only syntax spec. No EBNF, tokenization table, normative whitespace rules, escape table, or compatibility tests independent of this JS parser. For an explicitly versioned file format, implementation-defined syntax is not enough. | **Medium** |
| 10 | **Single global namespace, no imports or modules.** All rules share one flat namespace. No private helpers, no imports, no namespacing. Becomes noisy in large grammars. | **Medium** |

### New Correctness Bugs (from second-opinion review)

| # | Issue | Severity |
|---|-------|----------|
| B1 | **Discarded novelty candidates commit cooldown state.** `chooseProduction()` updates per-rule selection state immediately. `generate()` creates up to 12 candidates and keeps an earlier one. There is no state snapshot or rollback. After generation, cooldown history describes the final attempt, not the text returned. This weakens the documented structural anti-repetition guarantee. | **Medium** |
| B2 | <a id="audit-attribution"></a>**Repetition-audit attribution can blame text that didn't emit the fragment.** The replay pass increments counts for every step in the derivation trace, then `chooseLikelySource()` rewards greater trace depth. It does not know which character range a step emitted. A deep suffix rule can win attribution even though it emitted none of the repeated fragment. This is derivation correlation, not source attribution. | **Medium** |
| B3 | **Documented numeric options are not validated.** `noveltyAttempts: 0` throws a `TypeError` (not `MecoError`). Negative `noveltyWindow` or exact-window values cause iteration over `undefined`. Other non-finite or negative tuning values silently create `NaN`, infinite, or nonsensical weights. | **Medium** |
| B4 | **Compiled grammar is not actually immutable.** `Object.freeze()` on the outer object doesn't freeze the `Map`, rule objects, production arrays, or part objects. A caller can still `grammar.rules.set()`/`delete()` or mutate weights and parts. `MecoGenerator` precomputes metadata in its constructor; later grammar mutation makes those caches stale. | **Medium** |
| B5 | **Compositionality audit doesn't validate alternate start rule.** Generation and repetition audit validate their start rule; `auditComposition()` does not. An unknown name produces a generic `TypeError` instead of `MecoError("Unknown start rule ...")`. | **Medium** |
| B6 | **Productivity validation is not a termination guarantee.** The compiler proves only that each reachable rule has at least one derivation that can finish. A grammar with `[1] done` and `[99] @sentence@sentence` is accepted but has ~99% probability of exceeding runtime limits. Error messages should say "a terminating derivation exists," not imply guaranteed termination. | **Medium** |
| B7 | **Weights do not have one stable meaning across samplers.** Under `random`, a weight is a relative probability. Under `varied`, cooldown and diversity boosting alter it, except for recursion/empty-sensitive rules. Novelty search samples multiple complete derivations. The same `.meco` file does not define one probability distribution. Authors shouldn't read `[9]`/`[1]` as a persistent 90/10 contract unless using `random`. | **Medium** |

### Logic/Implementation Issues

| # | Issue | Severity |
|---|-------|----------|
| 8 | **`findRecursiveRules()` is O(V·(V+E)).** It runs a full DFS from every rule. A 1,000-rule grammar does 1,000 graph traversals. Tarjan's SCC algorithm would be O(V+E). Benchmarks confirm this is a real scaling problem for dense grammars, not merely theoretical. | **Medium** |
| 9 | **Productivity fixed-point worst case is O(V·(V+R)).** A long declaration-ordered chain needs up to V full passes. A reverse-dependency work queue would propagate productivity in O(V+R). | **Medium** |
| 10 | **Diversity estimator runs eagerly even for `selection: "random"`.** 16 iterations per production on generator construction — paid even when never used. | **Low** |
| 11 | **`chooseProduction` recomputes diversity weights on every invocation.** They depend only on immutable grammar structure. Precompute once at construction time. | **Low** |
| 12 | **`noveltyScore` and `rememberFragments` both call `candidateFragments`.** Fragment computation for the winning candidate is discarded then recomputed. ~2× regex work per generation. | **Medium** |
| 13 | **`rememberFragments` creates temporary `Set` and arrays on every call.** With high-frequency generation, this is GC pressure. | **Low** |
| 14 | **`reachableRuleNames` in `audit.js` duplicates reachability logic** already computed (and discarded) in `compile()`. Store the reachable set on the grammar object. | **Low** |
| 15 | <a id="line-number-corrections"></a>**Some compiler diagnostics use line 1 instead of the actual source line.** Unsupported versions and undefined start rules are both reported at line 1 even when their directives occur elsewhere. The compiler has the actual line number but doesn't retain it. | **Low** |
| 16 | **CLI argument parsing is fragile.** Manual loop doesn't handle `--count=5` syntax, and one option can consume the next flag as its value. Error wording is inconsistent by option. (The original review's claim that `--seed` without a value crashes is incorrect — it's explicitly checked and throws a clean `MecoError`.) | **Low** |
| 17 | **Locale-sensitive case folding.** Fragment normalization uses `toLocaleLowerCase()` without a fixed locale. Default-locale casing differs across hosts (notably Turkish dotted/dotless I), potentially producing different novelty collisions for the same seed. | **Low** |
| 18 | **Malformed weight syntax is silently accepted as literal text.** `[-1] text`, `[1.] text`, or `[1e3] text` do not produce diagnostics; the bracketed prefix becomes terminal output. This conflicts with the README's claim that invalid weights are checked. | **Low** |
| 19 | **Finite weights can overflow during selection.** Multiple individually-finite, extremely large decimal weights can sum to `Infinity`; diversity boosting can also overflow. The fallback then always selects the last production. | **Low** |

### Architectural/Approach Issues

| # | Issue | Severity |
|---|-------|----------|
| 20 | **`varied` is the default selection mode.** New users expecting "weighted random" get a stateful anti-repetition system instead. Opinionated default; the README is clear, but it's surprising. (Framed as a product choice for a v0.0.1 POC, not a defect.) | **Low** |
| 21 | **No TypeScript types, no JSDoc, no API docs beyond the README.** For something calling itself an API. | **Medium** |
| 22 | **No benchmark suite.** All performance claims were untested until the second-opinion review added synthetic microbenchmarks. | **Low** |
| 23 | **Test coverage is thin on failure paths.** No tests for: `@@` escape, decimal weights, CRLF handling, concurrent generators with different seeds, extreme weight distributions, nested recursion with mutual cycles, error message content, CLI subprocess coverage, cross-locale normalization, or sampler state after discarded candidates. (That said, the original review understated the suite: deterministic cross-instance generation and clean composition audits ARE tested.) | **Medium** |

---

## 3. Competitor Comparison

Two independent assessments were performed. The first (original review) weighted design-quality attributes that favor Mecojoni; the second (second opinion) used an equal-weight design-review rubric focused on authoring scalable generative dialogue. Neither includes community size, runtime speed, or commercial adoption — they evaluate the *language*, not its fame.

### Original Review Scoring (sum of 15 attributes, 1–10 each, max 150)

| Attribute | Mecojoni | Tracery | Rant | ink |
|---|---:|---:|---:|---:|
| Grammar expressiveness | 6 | 5 | 9 | 8 |
| Readability / authoring UX | 9 | 6 | 5 | 8 |
| Anti-repetition / novelty | 9 | 2 | 3 | 4 |
| Validation / error reporting | 8 | 3 | 5 | 7 |
| Reproducibility / seeding | 9 | 4 | 7 | 5 |
| Generation performance | 6 | 7 | 6 | 8 |
| Tooling / ecosystem | 1 | 8 | 6 | 9 |
| Learning curve (higher = easier) | 9 | 8 | 4 | 5 |
| Zero dependencies / portability | 10 | 9 | 4 | 5 |
| Community / documentation | 1 | 9 | 5 | 8 |
| Extensibility (modifiers/functions) | 1 | 7 | 9 | 8 |
| Recursion support | 8 | 2 | 7 | 5 |
| Traceability / debugging | 8 | 2 | 5 | 7 |
| Auditing / quality tools | 9 | 1 | 1 | 3 |
| Production readiness | 2 | 9 | 7 | 10 |
| **TOTAL** | **88** | **82** | **83** | **100** |

### Second-Opinion Scoring (sum of 11 attributes, 1–10 each, max 110)

| Attribute | Mecojoni | Tracery | Rant 4 | ink |
|---|---:|---:|---:|---:|
| Small-file readability | 9 | 6 | 5 | 8 |
| Basic recombination and recursion | 8 | 7 | 9 | 7 |
| Weighted/random selection control | 7 | 2 | 10 | 6 |
| Repetition and sequence control | 9 | 2 | 9 | 9 |
| State, conditions, and agreement | 1 | 4 | 10 | 10 |
| Value reuse and text transformations | 1 | 8 | 10 | 8 |
| Whitespace, literals, and multiline text | 2 | 6 | 8 | 9 |
| Syntax precision and specification | 3 | 4 | 8 | 8 |
| Validation and diagnostics | 8 | 3 | 7 | 10 |
| Large-project organization | 2 | 4 | 9 | 10 |
| Learnability | 9 | 8 | 4 | 6 |
| **TOTAL / 110** | **59** | **54** | **89** | **91** |

### Interpreting Both Tables

Both assessments agree on the fundamentals: Mecojoni wins at readability, anti-repetition, validation, and learnability. It loses badly at state/agreement, value reuse, tooling, ecosystem, and large-project organization.

Where they diverge is emphasis. The original table weighted design-quality attributes Mecojoni optimized for (giving it an 88 that flatters a 2-commit prototype). The second-opinion table weighted authoring-scalability attributes for real dialogue systems (giving it a 59 that reflects the gap between ambition and implementation). Both are honest; neither is "the" truth.

**What both tables agree on:** Mecojoni's `varied` sampler is unique. None of the three competitors have anything comparable. Tracery and Rant will both happily output the exact same sentence twice in a row. ink avoids this by being hand-authored — there is no "same sentence" because there's no combinatorial explosion to manage. **On anti-repetition, Mecojoni is currently best-in-class among CFG text generators.**

**Key context the scores don't capture:**
- **Tracery** — ~200 lines of engine, massive community, Cheap Bots Done Quick platform. Strength is *social*, not technical.
- **Rant 4** — by far the most powerful procedural generator (functions, variables, formatters, channels). Dense syntax, steep learning curve, C#/Rust runtime. Still alpha-labeled.
- **ink** — the only production-grade system. Full IDE (Inky), live preview, used in shipped commercial games (80 Days, Heaven's Vault). But it's for *interactive narrative* (player choices, branching), not pure CFG expansion.
- **Mecojoni** — best-in-class anti-repetition, excellent validation, zero dependencies, ~1,400 lines. But: no agreement/state system, no tooling, no community, 2 commits.

---

## 4. Performance Benchmarks

*Measured 2026-07-15 at commit `28f8193`, Apple M1 Max (10 cores, 64 GB), Node.js 26.5.0, single-threaded with `--expose-gc`. Medians of 5–7 timed runs after warmup. Synthetic grammars — real workloads will differ.*

### Compilation and Generator Construction

| Rules | Flat compile | Chain compile | Chain generator construction |
|---:|---:|---:|---:|
| 100 | 0.16 ms | 0.83 ms | 1.31 ms |
| 250 | 0.21 ms | 3.54 ms | 5.13 ms |
| 500 | 0.36 ms | 12.13 ms | 16.85 ms |
| 1,000 | 0.66 ms | 25.31 ms | 63.20 ms |

The same rule count changes compile time by ~38× depending on shape (flat vs chain). Chain generator construction grows ~48× when rule count grows 10× from 100 to 1,000.

### Dense Acyclic Construction

| Rules | Dependency edges | Generator construction |
|---:|---:|---:|
| 50 | 1,225 | 1.17 ms |
| 100 | 4,950 | 5.59 ms |
| 200 | 19,900 | 37.57 ms |
| 400 | 79,800 | 275.25 ms |

Increasing rule count 8× and edge count ~65× increased construction time ~236× — consistent with the repeated full-graph traversal in `findRecursiveRules()`. This is a real scaling problem for dense grammars.

### Generation Throughput (combinatorial grammar, 20,000 outputs)

| Selection mode | Time | Outputs/sec | Relative cost |
|---|---:|---:|---:|
| `random` | 21.04 ms | 950,627 | 1.0× |
| `varied`, 1 novelty attempt | 201.89 ms | 99,063 | 9.6× |
| `varied`, up to 12 attempts | 683.00 ms | 29,283 | 32.5× |

The default `varied` sampler is not a cheap embellishment — fragment normalization, history maintenance, candidate allocation, and retries dominate the work. Applications needing high throughput should benchmark with their actual collision rate and consider `random` or fewer novelty attempts.

### Exact-History Window Cliff

On a fixed-output workload with `noveltyAttempts: 1` and fragment history disabled:

| Range | Throughput |
|---|---|
| Outputs 40,001–50,000 (before eviction) | 149,706/sec |
| Outputs 50,001–60,000 (every call evicts) | 19,695/sec |

**7.6× slowdown** at the 50,000-entry boundary. Caused by `Array.prototype.shift()` on the full queue. A ring buffer or head-index queue is the highest-priority fix.

### Audit Cost (combinatorial grammar)

| Selection | 1,000 samples | 5,000 samples |
|---|---:|---:|
| `random` | 11.82 ms | 61.37 ms |
| `varied` | 68.89 ms | 364.83 ms |

Roughly linear in sample count for this grammar. Varied audit is ~6× slower because it pays novelty costs in both sampling and replay passes.

---

## 5. Review Audit — Claims Corrected

The second-opinion review identified several claims in the original review that were incorrect or overstated:

### False Claim: `weightedChoice` fallback is a wrong-production bug

**Original claim:** `target < cumulative` with `mulberry32` output near 1.0 could miss all branches, causing the fallback to pick the wrong production.

**Correction:** The PRNG returns a value strictly below 1. A rounded-up target at the total still belongs at the upper end of the last interval, and the fallback returns that last production — which is correct. Moreover, changing `<` to `<=` is **actively unsafe** when cooldown has made an earlier weight zero: a zero target could select a zero-weight production. The real numerical defect is weight overflow during selection, not the comparison operator. **Downgraded from CONFIRMED to NOT A BUG.**

### False Claim: `MecoError` during generation has no `diagnostics` array

**Original claim:** `MecoError` thrown during generation lacks a `diagnostics` array, causing the CLI handler to crash.

**Correction:** `MecoError`'s constructor defaults `diagnostics` to `[]`. The CLI iterates an empty array harmlessly, printing only the error message. **REVIEW.md issue 14 is false.**

### Overstated Claim: `[bracketed text]` is always parsed as a weight

**Original claim:** "`[` at position 0 is always parsed as a weight."

**Correction:** Only a leading positive numeric decimal in brackets is recognized as a weight (`/^\[(\d+(?:\.\d+)?)\]/`). `[bracketed text]` at position 0 would be literal text. The hole is narrower: `[3]` cannot start a production as literal text, but general bracket text works. **Downgraded from Medium to Low severity.**

### False Claim: Unicode rule names are valid

**Original claim:** Test coverage gap for "Unicode rule names (valid per the regex — e.g., `# café`)."

**Correction:** Both `RULE_NAME` and reference matching require `[A-Za-z]` as the initial character and `[A-Za-z0-9_-]*` for continuation. Unicode rule names are categorically invalid. The proposed test contradicts both code and README. **Removed from test gaps.**

### False Claim: `--seed` with no value crashes

**Original claim:** "`--seed` with no value produces `undefined` → crashes differently."

**Correction:** `--seed` without a value is explicitly checked (`if (options.seed === undefined) throw new MecoError(...)`). The real CLI weakness is that one option can consume the next flag as its value, and error wording is inconsistent. **Corrected.**

### Overstated Claim: `Object.freeze` makes the grammar immutable

**Original claim:** "`Object.freeze` on the compiled grammar is a nice defensive touch."

**Correction:** `Object.freeze()` is shallow. It doesn't freeze the `Map`, rule objects, production arrays, production objects, or part objects. A caller can still mutate the grammar, which would make `MecoGenerator`'s precomputed caches stale. **The immutability assumption underlying several review comments is incorrect.** This is now listed as bug B4 above.

### Downgraded: Diversity estimator, seed collisions, roadmap items

- **Diversity estimator's lack of formal convergence** is not a correctness defect — it's explicitly a bounded heuristic. Concrete quality failures or benchmarks are needed before treating it as a problem. **Downgraded from Medium to not a bug.**
- **32-bit seed hash collisions** are inherent to the design space. The API promises equal seeds → equal sequences, not injective mapping. Collision risk is a documented-design consideration, not a medium-severity bug. **Downgraded from Medium to not a bug.**
- **No IR, no bytecode, no serialization, no streaming, `varied` default** are product-roadmap choices for a v0.0.1 POC. They should not be presented as current medium-severity defects without a demonstrated requirement. **Reclassified as Low-severity architectural notes.**

---

## 6. Simplification Opportunities

*(Preserved from original review where still valid.)*

1. **Replace `findRecursiveRules` with Tarjan's SCC.** O(V·(V+E)) → O(V+E). Benchmarks confirm this matters for dense grammars.
2. **Replace productivity fixed-point with reverse-dependency work queue.** O(V·(V+R)) → O(V+R). Matters for long chains.
3. **Precompute diversity-boosted weights.** Compute once in constructor, apply only cooldown in `chooseProduction`.
4. **Unify fragment computation.** Return fragments from the winning candidate instead of recomputing in `rememberFragments`.
5. **Store `reachable` set on compiled grammar.** Avoid recomputing in `auditComposition()`.
6. **Replace exact-history `shift()` with a ring buffer.** Measured 7.6× cliff at 50,000 entries. Highest-priority fix.
7. **Replace `queue.shift()` in compile-time reachability with a head index.** Avoids engine-specific array shifting.
8. **Use `util.parseArgs()` for CLI argument parsing.** Handles `--flag=value`, validates arity, consistent errors.

---

## 7. Recommended Order of Work

*(From second-opinion review, based on measured impact.)*

1. **Define and test cooldown contract.** Decide whether cooldown history tracks attempted candidates or returned outputs. Refactor state handling to match (snapshot/rollback or explicit documentation).
2. **Fix exact-history eviction cliff.** Replace `shift()` with ring buffer or head-index queue. Add regression benchmark around the 50,000-entry boundary. *Measured 7.6× impact.*
3. **Fix audit attribution.** Replace trace-wide correlation with span-aware attribution, or rename the output so it doesn't overclaim causality.
4. **Add centralized option validation.** Validate all constructor options, throw `MecoError` with option name and required range. Validate start rule in `auditComposition()`.
5. **Make compiled grammar actually immutable.** Deep-freeze or expose through read-only wrappers. Alternately, remove caches that depend on immutability and document grammar as mutable.
6. **Fix deterministic normalization.** Use `toLowerCase()` instead of `toLocaleLowerCase()`. Handle malformed-weight diagnostics. Guard against weight overflow. Retain source lines for version/start diagnostics.
7. **If large/dense grammars are a target:** SCC for recursion, reverse-dependency work queue for productivity, precompute diversity weights, reuse winning candidate fragments.
8. **Build a benchmark suite with representative application grammars** before pursuing bytecode, serialization, inlining, or an optimizer IR.

---

## Summary

**Mecojoni is an unusually well-designed proof of concept with a best-in-class anti-repetition sampler.** Its format is genuinely readable, its compiler catches real errors, and its `varied` selection mode has no equivalent in Tracery, Rant, or ink. The core insight — that repetition resistance matters more than combinatorial breadth for dialogue — is correct and well-executed.

**The second-opinion review found real bugs the original missed** (candidate state pollution, audit misattribution, option validation gaps, false immutability) and corrected several overstated or incorrect claims in the original review (the `weightedChoice` "bug," `MecoError` diagnostics, `--seed` crash, Unicode rule names). **Benchmarks confirmed** that the exact-history queue has a measured 7.6× cliff, dense grammars scale poorly (236× construction time increase for 8× rules), and the default `varied` sampler is ~32× costlier than `random`.

**Its weaknesses are a mix of scale problems and fundamental gaps.** The scale problems (no tooling, no community, no types, no benchmarks, O(V·(V+E)) algorithms) are fixable with investment. The fundamental gaps (no agreement/state system, no value reuse, brittle whitespace semantics, no language specification) are design decisions that limit the format's ceiling for large dialogue systems. Calling the format "Markdown-like" describes its appearance accurately but overstates its maturity — Markdown has quoting, escaping, nesting, and structure/content separation that Mecojoni lacks.

**Bottom line:** If you want to ship a game tomorrow, use ink or Tracery. If you want the best *foundation* for a dialogue grammar system and are willing to invest in tooling, Mecojoni's design is a better starting point than either — but fix the measured queue and graph costs, tighten the candidate-state and attribution contracts, and honestly scope the format's ambitions before adding features. The question is whether the 1,436 lines become a project or remain a prototype.
