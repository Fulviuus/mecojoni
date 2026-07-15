import assert from "node:assert/strict";
import test from "node:test";
import { auditComposition, auditGrammar } from "../src/audit.js";
import { compile, edgeFragments, MecoError, MecoGenerator } from "../src/meco.js";

const QUALITY_GRAMMAR = `@meco 1
@start sentence

# sentence
- @speaker @observation @place.
- @speaker @discovery @place.

# speaker
- The pilot
- A mechanic
- My neighbour
- The courier

# observation
- noticed @object
- carefully inspected @object
- asked about @object
- was looking for @object

# discovery
- found @object
- misplaced @object
- repaired @object
- brought back @object

# object
- the old radio
- a missing toolkit
- an unusual package
- the navigation chart

# place
- near the workshop
- beside the market
- outside the library
- under the eastern bridge
`;

const NOVELTY_GRAMMAR = `@meco 1
@start statement

# statement
- @subject @time @action @object @place; @witness @response @manner.

# subject
- Pilots
- Gardeners
- Couriers
- Mechanics
- Teachers
- Musicians
- Bakers
- Sailors

# time
- recently
- quietly
- carefully
- unexpectedly
- yesterday
- promptly
- briefly
- cheerfully

# action
- inspected
- repaired
- moved
- counted
- polished
- delivered
- tested
- collected

# object
- lanterns
- radios
- parcels
- notebooks
- bicycles
- instruments
- baskets
- compasses

# place
- nearby
- outside
- downstairs
- ashore
- indoors
- uphill
- backstage
- homeward

# witness
- neighbours
- visitors
- children
- workers
- merchants
- artists
- travellers
- officials

# response
- nodded
- laughed
- waited
- whispered
- applauded
- listened
- wondered
- agreed

# manner
- politely
- nervously
- patiently
- together
- silently
- warmly
- cautiously
- eventually
`;

test("compiles and generates a hierarchy-driven grammar", () => {
  const grammar = compile(QUALITY_GRAMMAR, { filename: "quality.meco" });
  const generator = new MecoGenerator(grammar, { seed: "quality-check" });

  assert.equal(grammar.start, "sentence");
  assert.equal(
    auditComposition(grammar).violations.length,
    0,
    "the composed test grammar must pass its structural audit",
  );

  for (let index = 0; index < 250; index += 1) {
    const { text, trace } = generator.generate();
    assert.match(text, /^[A-Z]/);
    assert.match(text, /[.!?”]$/);
    assert.doesNotMatch(text, /@[A-Za-z]/);
    assert.ok(text.length < 600, `unexpectedly long output: ${text}`);
    assert.ok(trace.length > 1);
  }
});

test("a seed reproduces the same sequence", () => {
  const grammar = compile(QUALITY_GRAMMAR);
  const first = new MecoGenerator(grammar, { seed: "42" });
  const second = new MecoGenerator(grammar, { seed: "42" });

  const firstSequence = Array.from({ length: 20 }, () => first.generate().text);
  const secondSequence = Array.from({ length: 20 }, () => second.generate().text);
  assert.deepEqual(firstSequence, secondSequence);
});

test("reports undefined nonterminals with their source line", () => {
  const source = `@meco 1
@start sentence

# sentence
- Hello, @person.
`;

  assert.throws(
    () => compile(source, { filename: "broken.meco" }),
    (error) => {
      assert.ok(error instanceof MecoError);
      assert.match(error.diagnostics.join("\n"), /broken\.meco:5:.*undefined rule @person/);
      return true;
    },
  );
});

test("supports recursive context-free productions", () => {
  const grammar = compile(`@meco 1
@start balanced

# balanced
- [5] ()
- [1] (@balanced)
- [1] @balanced@balanced
`);
  const generator = new MecoGenerator(grammar, { seed: "recursive", maxDepth: 100 });

  for (let index = 0; index < 100; index += 1) {
    const { text } = generator.generate();
    let balance = 0;
    for (const character of text) {
      balance += character === "(" ? 1 : -1;
      assert.ok(balance >= 0, `unbalanced prefix in ${text}`);
    }
    assert.equal(balance, 0);
  }
});

test("varied selection does not immediately reuse a rule production", () => {
  const grammar = compile(`@meco 1
@start sentence

# sentence
- @choice

# choice
- @alpha
- @beta
- @gamma

# alpha
- Alpha.

# beta
- Beta.

# gamma
- Gamma.
`);
  const generator = new MecoGenerator(grammar, {
    seed: "cooldown",
    noveltyAttempts: 1,
  });

  const sequence = Array.from({ length: 30 }, () => generator.generate().text);
  for (let index = 1; index < sequence.length; index += 1) {
    assert.notEqual(sequence[index], sequence[index - 1]);
  }
});

test("varied selection preserves author weights for optional empty rules", () => {
  const grammar = compile(`@meco 1
@start sentence

# sentence
- Hello@optional.

# optional
- [9] @empty
- [1] @suffix

# suffix
- there
`);
  const generator = new MecoGenerator(grammar, {
    seed: "optional-weight",
    noveltyAttempts: 1,
  });
  let included = 0;
  for (let index = 0; index < 2_000; index += 1) {
    if (generator.generate().text === "Hellothere.") included += 1;
  }

  assert.ok(included > 100 && included < 300, `unexpected optional count: ${included}`);
});

test("supports the @empty built-in and the legacy epsilon alias", () => {
  const grammar = compile(`@meco 1
@start ascii-empty

# ascii-empty
- @empty

# legacy-empty
- ε
`);
  const generator = new MecoGenerator(grammar, { selection: "random" });

  assert.equal(generator.generate({ start: "ascii-empty" }).text, "");
  assert.equal(generator.generate({ start: "legacy-empty" }).text, "");
});

test("reserves the empty rule name for the @empty built-in", () => {
  assert.throws(
    () => compile(`@meco 1
@start empty

# empty
- text
`),
    (error) => {
      assert.ok(error instanceof MecoError);
      assert.match(error.diagnostics.join("\n"), /rule name empty is reserved/);
      return true;
    },
  );
});

test("empty production diagnostics recommend @empty", () => {
  assert.throws(
    () => compile(`@meco 1
@start sentence

# sentence
-
`),
    (error) => {
      assert.ok(error instanceof MecoError);
      assert.match(error.diagnostics.join("\n"), /use @empty for an empty production/);
      return true;
    },
  );
});

test("the audit reports repeated sentence fragments and their grammar source", () => {
  const grammar = compile(`@meco 1
@start sentence

# sentence
- @person checked the notice. The room became quiet again.

# person
- A teacher
- A neighbour
`);
  const report = auditGrammar(grammar, {
    samples: 100,
    seed: "repetition-audit",
    selection: "random",
  });
  const repeatedEnding = report.repeated.find(
    (fragment) => fragment.kind === "suffix" && fragment.text === "the room became quiet again",
  );

  assert.ok(repeatedEnding);
  assert.equal(repeatedEnding.count, 100);
  assert.equal(repeatedEnding.source.step.rule, "sentence");
});

test("fragment tracking includes internal sentence boundaries", () => {
  const fragments = edgeFragments(
    "A bell rang. The morning crowd gathered quietly.",
    { minWords: 3, maxWords: 4 },
  );

  assert.ok(
    fragments.some(
      (fragment) =>
        fragment.kind === "sentence-prefix" && fragment.text === "the morning",
    ),
  );
  assert.ok(
    fragments.some(
      (fragment) => fragment.kind === "sentence-suffix" && fragment.text === "a bell rang",
    ),
  );
});

test("the composition audit rejects fixed sentence shells", () => {
  const grammar = compile(`@meco 1
@start sentence

# sentence
- The guard rejected the request because @reason.

# reason
- the seal is missing
`);
  const report = auditComposition(grammar);

  assert.equal(report.violations.length, 1);
  assert.equal(report.violations[0].rule, "sentence");
  assert.equal(report.violations[0].semanticParts, 1);
  assert.equal(report.violations[0].longestFixedRun, 6);
});

test("the composition audit accepts semantic role assembly", () => {
  const grammar = compile(`@meco 1
@start sentence

# sentence
- @actor @decision @object @reason-link @reason.

# actor
- The reviewer
# decision
- approved
# object
- the request
# reason-link
- because
# reason
- the records matched
`);

  assert.equal(auditComposition(grammar).violations.length, 0);
});

test("stress-generates a nested grammar", () => {
  const grammar = compile(NOVELTY_GRAMMAR, { filename: "novelty.meco" });
  const generator = new MecoGenerator(grammar, { seed: "stress-quality-check" });

  assert.ok(grammar.rules.size > 5);
  assert.equal(grammar.start, "statement");
  assert.equal(auditComposition(grammar).violations.length, 0);
  for (let index = 0; index < 1_000; index += 1) {
    const { text } = generator.generate();
    assert.match(text, /^[A-Z]/);
    assert.doesNotMatch(text, /@[A-Za-z]/);
    assert.match(text, /[.!?”]$/);
    assert.ok(text.length < 600, `unexpectedly long output: ${text}`);
  }
});

test("phrases avoid recently used openings and endings", () => {
  const grammar = compile(NOVELTY_GRAMMAR);
  const generator = new MecoGenerator(grammar, { seed: "novelty-regression" });
  const recent = [];

  for (let index = 0; index < 500; index += 1) {
    const result = generator.generate();

    const fragments = new Set(
      edgeFragments(result.text, { minWords: 3, maxWords: 8 })
        .filter((fragment) => fragment.kind !== "exact")
        .map((fragment) => fragment.key),
    );
    const recentFragments = new Set(recent.flatMap((items) => [...items]));
    if (result.noveltyAttempts < 12) {
      assert.ok(
        [...fragments].every((fragment) => !recentFragments.has(fragment)),
        `reused a recent edge fragment in: ${result.text}`,
      );
    }
    recent.push(fragments);
    if (recent.length > 300) recent.shift();
  }
});
