<p align="center">
  <img src="assets/mecojoni-logo.png" alt="Mecojoni" width="760">
</p>

<p align="center">
  A readable context-free grammar format for generative dialogue and text.
</p>

# Mecojoni

Mecojoni is an experimental language and runtime for writing large generative
text systems in plain-text `.meco` files. It is intended for games, simulations,
procedural worlds, interactive fiction, test-data generation, and any project
that needs more structure than choosing a random line from a list.

A Mecojoni grammar is a
[context-free grammar](https://en.wikipedia.org/wiki/Context-free_grammar):
named rules expand into terminal text or other named rules. Expansions can be
nested, weighted, optional, and recursive. The runtime validates the complete
rule graph before generation and can produce deterministic sequences from a
seed.

The format aims to feel like the Markdown of generative text: approachable in a
text editor, easy to review in source control, and expressive enough to grow
from a dozen greetings into a large dialogue hierarchy.

> **Project status:** early proof of concept. Format version `1`, the JavaScript
> API, and the command-line interface may change while the core design is being
> explored.

## Why Mecojoni?

Simple phrase generators tend to choose a complete sentence or replace a few
placeholders. That works for small datasets, but its seams become obvious when
characters speak repeatedly. Mecojoni instead models the hierarchy that creates
a sentence:

```text
dialogue
└── observation
    ├── observer
    ├── event
    │   ├── action
    │   └── object
    ├── place
    └── reaction
```

This provides several useful properties:

- **Compositional range:** a modest number of carefully scoped rules can create
  a large number of distinct derivations.
- **Semantic structure:** authors can keep actions, objects, causes, and
  reactions together in compatible subtrees.
- **Controlled probability:** production weights make common, unusual, and rare
  outcomes explicit.
- **Reproducibility:** identical seeds and call sequences generate identical
  output sequences.
- **Validation:** broken references and non-terminating reachable rules are
  rejected before any text is generated.
- **Repetition resistance:** the default sampler remembers recent structures,
  openings, endings, and exact lines.
- **Traceability:** every generated phrase can be traced back to the productions
  and source lines that created it.

Mecojoni deliberately separates two concerns: the grammar defines what may be
said, while the sampler decides which valid derivation to choose next.

## Requirements

- Node.js 20 or newer
- No external npm dependencies

Check your Node.js version:

```sh
node --version
```

Run the test suite:

```sh
npm test
```

## Quick start

Create a file named `hello.meco`:

```meco
@meco 1
@start greeting

# greeting
- [3] @salutation, @person!
- [1] @person, @observation.

# salutation
- Hello
- Good morning
- Welcome

# person
- traveller
- neighbour
- old friend

# observation
- the weather has changed
- the market is unusually quiet
- today feels promising
```

Generate five phrases:

```sh
node src/cli.js hello.meco --count 5
```

Generate the same sequence again whenever you use the same seed:

```sh
node src/cli.js hello.meco --count 5 --seed demo-world
```

Inspect the derivation behind each result:

```sh
node src/cli.js hello.meco --count 2 --seed demo-world --trace
```

## The `.meco` format

Every format-version-1 file needs a version directive, a start directive, and
at least one rule:

```meco
@meco 1
@start sentence

# sentence
- @subject @predicate.

# subject
- The pilot
- A maintenance drone

# predicate
- is waiting
- found the missing tool
```

Generation begins at `@start`. The chosen production is expanded from left to
right. Every referenced nonterminal is recursively expanded until only terminal
text remains.

### Directives

| Syntax | Meaning |
| --- | --- |
| `@meco 1` | Declares the file-format version. Version `1` is currently supported. |
| `@start rule-name` | Declares the default rule used for generation. |

Each directive must appear exactly once.

### Rules and productions

A heading defines a nonterminal. Each list item beneath it defines one possible
production:

```meco
# temperature
- cold
- mild
- uncomfortably warm
```

Rule names must begin with an ASCII letter and may then contain letters,
numbers, hyphens, or underscores. Names are case-sensitive.

### References

Use `@name` to expand another rule:

```meco
# report
- The @device is @condition.

# device
- air recycler
- navigation console

# condition
- offline
- making a strange noise
```

References can appear anywhere in a production and may be adjacent to terminal
text or other references.

### Weights

Place a positive number in square brackets at the beginning of a production to
give it a relative weight. An omitted weight is `1`:

```meco
# mood
- [6] calm
- [3] tired
- [1] furious
```

Under independent `random` selection, the example above chooses `calm` about
60% of the time, `tired` about 30%, and `furious` about 10%. Weights do not need
to add up to any particular total, and decimal weights are valid.

### Empty productions and optional text

The ASCII built-in `@empty` represents an empty production:

```meco
# greeting
- Welcome, @name@title-option.

# title-option
- [3] @empty
- [1]  the @title

# name
- Ada
- Tomas

# title
- Captain
- Doctor
```

`@empty` is deliberately easy to type, search for, and recognize in any editor.
The conventional mathematical symbol `ε` remains accepted as a compatibility
alias, but authors never need to type it.

Rules containing an empty production are probability-sensitive. The varied
sampler preserves their author-defined weights instead of applying cooldown or
diversity boosts. The rule name `empty` is reserved for this built-in.

### Recursion

Rules may reference themselves directly or indirectly:

```meco
# inventory
- [5] @item
- [1] @item, @inventory

# item
- a coil of wire
- a repair kit
- an empty canister
```

Every reachable recursive cycle must have a path that eventually produces only
terminal text. Recursive rule weights should strongly favour termination.
Mecojoni preserves those weights and enforces generation depth and expansion
limits as a final safety net.

### Comments

Lines whose first non-whitespace characters are `//` are ignored. Inline
comments are not currently supported.

```meco
// Dialogue used during calm conditions.
# calm-line
- Everything is under control.
```

### Literal `@` characters

Because `@` begins a rule reference, write `@@` to emit one literal `@`:

```meco
# contact
- Send a message to pilot@@example.invalid.
```

## Thinking in context-free grammars

In grammar terminology:

- a `# rule` is a **nonterminal**;
- a `- production` is one possible replacement for that nonterminal;
- literal text is a **terminal**;
- `@start` identifies the start symbol;
- generation produces a derivation from the start symbol to terminal text.

For example, this rule:

```meco
# sentence
- @actor @action @object.
```

corresponds conceptually to:

```text
sentence → actor action object "."
```

The most important authoring decision is not how many combinations a grammar
has, but whether each combination makes sense. A completely unrestricted
`actor × action × object` product can be enormous and still produce weak prose.
Prefer semantic branches whose members share a contract:

```meco
# incident
- @repair-incident
- @travel-incident

# repair-incident
- @technician @repair-action @broken-device.

# travel-incident
- @traveller @travel-action @destination.
```

Here, repair actions can only receive repair-compatible objects, while travel
actions can only receive destinations. Variation remains large without
sacrificing coherence.

## Selection modes

Mecojoni supports two sampling modes.

| Mode | Behavior | Best suited for |
| --- | --- | --- |
| `varied` | Stateful, repetition-resistant selection; the default | NPC dialogue and repeated player-facing text |
| `random` | Independent weighted CFG sampling with no novelty memory | Probability testing and applications that require unmodified draws |

Choose a mode on the command line:

```sh
node src/cli.js dialogue.meco --count 100 --selection varied
node src/cli.js dialogue.meco --count 100 --selection random
```

### How varied selection works

The varied sampler combines three mechanisms:

1. **Structural cooldown:** recently selected structural productions receive a
   temporary penalty, with immediate reuse normally prevented.
2. **Subtree diversity:** productions leading to more distinct derivations can
   receive a bounded boost so large branches are not hidden behind one small
   parent choice.
3. **Output novelty:** the runtime can consider up to 12 candidate derivations
   and prefer the one whose visible opening and ending fragments were used least
   recently.

By default, visible 3–8 word openings and endings are remembered across the
previous 300 generated phrases. Two-word fragments are also tracked at internal
sentence boundaries. Exact normalized lines are remembered across the previous
50,000 phrases.

Recursive rules and rules containing `@empty` are exempt from structural
cooldowns and diversity boosts, because their weights commonly control
termination and optionality.

Novelty state belongs to a `MecoGenerator` instance. Reuse one generator for a
dialogue pool when nearby NPCs should avoid each other's recent phrasing. If
every NPC receives a separate generator, each NPC receives separate repetition
memory.

## Command-line interface

```text
Usage: meco <grammar.meco> [options]
```

| Option | Description |
| --- | --- |
| `-n, --count <number>` | Number of phrases to generate. Default: `1`. |
| `--seed <value>` | Seeds the deterministic pseudorandom sequence. |
| `--start <rule>` | Generates from a named rule instead of the file's `@start`. |
| `--selection <mode>` | Selects `varied` or `random`. Default: `varied`. |
| `--audit <samples>` | Audits exact lines and repeated opening/ending fragments. |
| `--audit-composition` | Audits sentence productions for large fixed literal shells. |
| `--trace` | Prints selected productions and source lines to standard error. |
| `-h, --help` | Prints command help. |

### Alternative start rules

A grammar can expose multiple useful entry points even though only one is the
default:

```sh
node src/cli.js dialogue.meco --start greeting --count 10
node src/cli.js dialogue.meco --start warning --count 10
```

### Seeds

Seeds are strings. Reproducibility depends on all of the following remaining
the same:

- grammar contents;
- seed;
- selection mode and generator options;
- requested start rules;
- number and order of previous calls on that generator.

Changing any one of them may change the sequence.

### Derivation traces

`--trace` leaves generated text on standard output and prints a tree-shaped
derivation trace to standard error. Each trace entry includes the rule name,
one-based production number, source line, and expansion depth. The trace header
also reports the number of candidate attempts used by novelty selection.

This is useful when a sentence is awkward and you need to identify the exact
branch that assembled it.

## JavaScript API

Mecojoni currently exposes ES modules directly from `src/`.

### Compile once, generate many times

```js
import { readFile } from "node:fs/promises";
import { compile, MecoGenerator } from "./src/meco.js";

const source = await readFile("dialogue.meco", "utf8");
const grammar = compile(source, { filename: "dialogue.meco" });

const generator = new MecoGenerator(grammar, {
  seed: "world-1042",
  selection: "varied",
});

for (let index = 0; index < 10; index += 1) {
  const result = generator.generate();
  console.log(result.text);
}
```

`compile()` parses and validates source text into an in-memory grammar. It does
not currently write a compiled artifact to disk. Applications should compile a
grammar once during loading and reuse the compiled object.

### Generate from a different rule

```js
const result = generator.generate({ start: "warning" });

console.log(result.text);
console.log(result.noveltyAttempts);
console.log(result.noveltyScore);
console.log(result.trace);
```

### Generator options

| Option | Default | Purpose |
| --- | ---: | --- |
| `seed` | `"mecojoni"` | Reproducible random seed. |
| `selection` | `"varied"` | `varied` or `random`. |
| `maxDepth` | `80` | Maximum recursive expansion depth. |
| `maxExpansions` | `2000` | Maximum rule expansions in one candidate. |
| `noveltyAttempts` | `12` | Maximum candidates considered per varied generation. |
| `noveltyWindow` | `300` | Number of recent phrases retained for edge fragments. |
| `exactNoveltyWindow` | `50000` | Number of recent exact lines retained. |
| `fragmentMinWords` | `3` | Minimum normal edge-fragment length. |
| `fragmentMaxWords` | `8` | Maximum edge-fragment length. |

Advanced cooldown and diversity parameters are also available in the
constructor, but they should be treated as experimental implementation details
until the API stabilizes.

### Handling compilation errors

```js
import { compile, MecoError } from "./src/meco.js";

try {
  const grammar = compile(source, { filename: "dialogue.meco" });
} catch (error) {
  if (error instanceof MecoError) {
    console.error(error.message);
    for (const item of error.diagnostics) console.error(item);
  } else {
    throw error;
  }
}
```

Diagnostics include the supplied filename and source line whenever possible.

## Compilation and validation

Before returning a grammar, `compile()` checks:

- missing, duplicate, or unsupported `@meco` declarations;
- missing or duplicate `@start` declarations;
- invalid or undefined start rules;
- invalid and duplicate rule names;
- productions outside rule sections;
- empty productions that should use `@empty`;
- invalid or non-positive weights;
- malformed `@` references;
- references to undefined rules;
- rules without productions;
- reachable rules that can never finish producing terminal text.

Compilation is currently an in-memory parse and validation step performed each
time a process loads a `.meco` file. A serialized or binary compiled format is a
future possibility, but no `.mecoc` format exists yet.

During generation, `maxDepth` and `maxExpansions` protect the host application
from unexpectedly deep or explosive recursive derivations.

## Auditing a grammar

Tests prove that the runtime behaves as intended; audits help evaluate the
content of a particular grammar.

### Repetition audit

Generate a large deterministic sample and find repeated complete lines,
openings, endings, and internal sentence boundaries:

```sh
node src/cli.js dialogue.meco --audit 25000 --seed audit
```

The report includes total and unique lines, exact duplicates, novelty retries,
the most frequent edge fragments, and the derivation step most likely
responsible for each repeated fragment. Add `--selection random` to audit
independent weighted sampling.

### Compositionality audit

The optional structural audit identifies sentence-level productions that may
contain too much fixed prose:

```sh
node src/cli.js dialogue.meco --audit-composition
```

For each reachable production ending in sentence punctuation, it expects at
least three nonterminal references and no terminal run longer than two words.
This deliberately strict heuristic is useful for finding large fixed sentence
shells. It is not a general measure of writing quality, and a curated grammar
may intentionally violate it. Use `--start <rule>` to audit a particular
subtree.

## Authoring guidance

Large output counts alone do not make dialogue convincing. These practices tend
to produce better results:

1. **Compose within a concept.** Give each subtree a semantic contract, such as
   repair events, travel delays, or medical paperwork.
2. **Vary visible edges.** Repeated openings and endings are noticed much sooner
   than repeated words in the middle of a sentence.
3. **Split incompatible categories.** If only some verbs accept some objects,
   create separate branches instead of one global verb and object pool.
4. **Use weights for world texture.** Commonplace events should remain common;
   rare events feel rare only when their probability is intentional.
5. **Keep reactions contextual.** A reaction that follows any event will
   eventually follow an event it does not fit.
6. **Treat recursion carefully.** Always provide a strongly weighted terminating
   path and test maximum observed output length.
7. **Audit generated corpora.** Grammar review cannot reveal every interaction
   between distant branches.
8. **Use traces to repair the tree.** Fix awkward combinations at the smallest
   rule that violated its semantic contract.

For procedural games, a practical arrangement is to compile each dialogue
grammar once, create one generator per repetition domain, and derive stable
seeds from world identifiers. A location-wide generator shares novelty memory
between NPCs; an NPC-specific generator isolates each character's sequence.

## Project structure

```text
assets/
  mecojoni-logo.png   Project logo
src/
  meco.js             Compiler, generator, seeding, and novelty selection
  audit.js            Repetition and compositionality audits
  cli.js              Command-line interface
test/
  meco.test.js        Compiler, generator, audit, and regression tests
package.json          Node.js package metadata and scripts
README.md             Project documentation
```

## Development

```sh
npm test
node src/cli.js --help
```

The project intentionally has no runtime dependencies. Tests use Node.js's
built-in test runner and assertion library.

## Current limitations

Mecojoni version 1 does not yet provide:

- variables or values supplied by a host game;
- grammatical features such as number, gender, tense, or agreement;
- conditions, tags, or rule parameters;
- imports, modules, or namespacing across files;
- transforms such as capitalization or article selection;
- persistence of novelty history across process restarts;
- a serialized compiled grammar artifact;
- editor tooling or a language server;
- a stable published npm API.

These are intentional boundaries of the current proof of concept, not promises
that every item will enter the language unchanged.

## Direction

Likely areas for further exploration include:

- host-supplied context without turning the format into a template engine;
- typed or feature-aware nonterminals for grammatical agreement;
- grammar modules and reusable vocabulary packages;
- a compiled distribution format for game builds;
- corpus-level quality metrics and interactive derivation inspection;
- bindings for game engines and other host languages;
- a formal versioned format specification.

The goal is a small author-facing language that remains readable as its grammar
becomes deep, while giving runtime systems enough information to generate
varied, coherent text for a long-lived procedural world.

## Name

“Mecojoni” is Roman slang loosely conveying “wow.” The name is unrelated to the
grammar model; it was chosen because it is memorable and fun to say.
