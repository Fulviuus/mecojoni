import { edgeFragments, MecoGenerator } from "./meco.js";

function traceKey(step) {
  return `${step.rule}:${step.production}:${step.line}`;
}

function chooseLikelySource(pathCounts, occurrenceCount) {
  let best = null;
  for (const { count, step } of pathCounts.values()) {
    const coverage = count / occurrenceCount;
    const score = coverage * coverage * (step.depth + 1);
    if (best === null || score > best.score) {
      best = { score, coverage, step };
    }
  }
  return best;
}

export function auditGrammar(grammar, options = {}) {
  const samples = options.samples ?? 10_000;
  const seed = String(options.seed ?? "mecojoni-audit");
  const selection = options.selection ?? "varied";
  const start = options.start ?? grammar.start;
  const minWords = options.minWords ?? 3;
  const maxWords = options.maxWords ?? 8;
  const limit = options.limit ?? 20;
  const generatorOptions = { seed, selection };

  const generator = new MecoGenerator(grammar, generatorOptions);
  const exact = new Set();
  const fragments = new Map();
  let exactDuplicates = 0;
  let noveltyRetries = 0;

  for (let index = 0; index < samples; index += 1) {
    const result = generator.generate({ start });
    const normalized = result.text.toLocaleLowerCase();
    if (exact.has(normalized)) exactDuplicates += 1;
    else exact.add(normalized);
    noveltyRetries += result.noveltyAttempts - 1;

    for (const fragment of edgeFragments(result.text, { minWords, maxWords })) {
      if (fragment.kind === "exact") continue;
      const existing = fragments.get(fragment.key);
      if (existing) existing.count += 1;
      else fragments.set(fragment.key, { ...fragment, count: 1 });
    }
  }

  const repeated = [...fragments.values()]
    .filter((fragment) => fragment.count > 1)
    .sort((left, right) => right.count - left.count || right.size - left.size)
    .slice(0, limit);

  // Re-run the deterministic sequence and identify the deepest derivation step
  // common to each repeated edge fragment.
  const targets = new Map(repeated.map((fragment) => [fragment.key, fragment]));
  const paths = new Map(repeated.map((fragment) => [fragment.key, new Map()]));
  const replay = new MecoGenerator(grammar, generatorOptions);
  for (let index = 0; index < samples; index += 1) {
    const result = replay.generate({ start });
    const outputFragments = edgeFragments(result.text, { minWords, maxWords });
    for (const fragment of outputFragments) {
      if (!targets.has(fragment.key)) continue;
      const pathCounts = paths.get(fragment.key);
      for (const step of result.trace) {
        const key = traceKey(step);
        const existing = pathCounts.get(key);
        if (existing) existing.count += 1;
        else pathCounts.set(key, { count: 1, step });
      }
    }
  }

  for (const fragment of repeated) {
    fragment.source = chooseLikelySource(paths.get(fragment.key), fragment.count);
  }

  return {
    samples,
    seed,
    selection,
    unique: exact.size,
    exactDuplicates,
    noveltyRetries,
    repeated,
  };
}

export function formatAudit(report) {
  const lines = [
    `Mecojoni repetition audit`,
    `Samples: ${report.samples}`,
    `Selection: ${report.selection}`,
    `Unique complete lines: ${report.unique}`,
    `Exact duplicates: ${report.exactDuplicates}`,
    `Novelty retries: ${report.noveltyRetries}`,
    ``,
    `Most frequent openings and endings:`,
  ];

  if (report.repeated.length === 0) {
    lines.push("  No repeated edge fragments found.");
    return lines.join("\n");
  }

  for (const fragment of report.repeated) {
    const rate = ((fragment.count / report.samples) * 100).toFixed(3);
    lines.push(
      `  ${fragment.kind} (${fragment.size} words): ${JSON.stringify(fragment.text)}`,
      `    ${fragment.count}/${report.samples} (${rate}%)`,
    );
    if (fragment.source) {
      const { step } = fragment.source;
      lines.push(
        `    likely source: @${step.rule} production ${step.production}, line ${step.line}`,
      );
    }
  }

  return lines.join("\n");
}

function reachableRuleNames(grammar, start) {
  const reachable = new Set([start]);
  const pending = [start];
  while (pending.length > 0) {
    const name = pending.pop();
    const rule = grammar.rules.get(name);
    if (!rule) continue;
    for (const production of rule.productions) {
      for (const part of production.parts) {
        if (part.type === "nonterminal" && !reachable.has(part.name)) {
          reachable.add(part.name);
          pending.push(part.name);
        }
      }
    }
  }
  return reachable;
}

function terminalWordCount(text) {
  return text.match(/[\p{L}\p{N}]+(?:['’][\p{L}\p{N}]+)*/gu)?.length ?? 0;
}

export function auditComposition(grammar, options = {}) {
  const start = options.start ?? grammar.start;
  const maxFixedWords = options.maxFixedWords ?? 2;
  const minSemanticParts = options.minSemanticParts ?? 3;
  const reachable = reachableRuleNames(grammar, start);
  const violations = [];

  for (const name of reachable) {
    const rule = grammar.rules.get(name);
    for (let index = 0; index < rule.productions.length; index += 1) {
      const production = rule.productions[index];
      if (!/[.!?](?:[”’"'])?\s*$/.test(production.source)) continue;

      const semanticParts = production.parts.filter(
        (part) => part.type === "nonterminal",
      ).length;
      const longestFixedRun = Math.max(
        0,
        ...production.parts
          .filter((part) => part.type === "terminal")
          .map((part) => terminalWordCount(part.value)),
      );

      if (semanticParts < minSemanticParts || longestFixedRun > maxFixedWords) {
        violations.push({
          rule: name,
          production: index + 1,
          line: production.line,
          source: production.source,
          semanticParts,
          longestFixedRun,
        });
      }
    }
  }

  return {
    start,
    reachableRules: reachable.size,
    maxFixedWords,
    minSemanticParts,
    violations,
  };
}

export function formatCompositionAudit(report) {
  const lines = [
    "Mecojoni compositionality audit",
    `Start: @${report.start}`,
    `Reachable rules: ${report.reachableRules}`,
    `Sentence requirements: at least ${report.minSemanticParts} semantic parts; no fixed run over ${report.maxFixedWords} words`,
    `Violations: ${report.violations.length}`,
  ];

  for (const violation of report.violations.slice(0, 50)) {
    lines.push(
      `  @${violation.rule} production ${violation.production}, line ${violation.line}`,
      `    semantic parts: ${violation.semanticParts}; longest fixed run: ${violation.longestFixedRun}`,
      `    ${violation.source}`,
    );
  }
  if (report.violations.length > 50) {
    lines.push(`  …and ${report.violations.length - 50} more.`);
  }
  return lines.join("\n");
}
