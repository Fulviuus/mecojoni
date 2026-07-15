export class MecoError extends Error {
  constructor(message, diagnostics = []) {
    super(message);
    this.name = "MecoError";
    this.diagnostics = diagnostics;
  }
}

const RULE_NAME = /^[A-Za-z][A-Za-z0-9_-]*$/;

function diagnostic(filename, line, message) {
  return `${filename}:${line}: ${message}`;
}

function parseProduction(text, filename, line) {
  // The mathematical symbol remains a compatibility alias. The keyboard-
  // friendly @empty built-in is parsed with other references below.
  if (text === "ε") {
    return [];
  }

  if (text.length === 0) {
    throw new MecoError("Invalid empty production", [
      diagnostic(filename, line, "use @empty for an empty production"),
    ]);
  }

  const parts = [];
  let literal = "";
  let cursor = 0;

  const flushLiteral = () => {
    if (literal.length > 0) {
      parts.push({ type: "terminal", value: literal });
      literal = "";
    }
  };

  while (cursor < text.length) {
    if (text[cursor] !== "@") {
      literal += text[cursor];
      cursor += 1;
      continue;
    }

    // A doubled @@ produces one literal @.
    if (text[cursor + 1] === "@") {
      literal += "@";
      cursor += 2;
      continue;
    }

    const match = text.slice(cursor + 1).match(/^[A-Za-z][A-Za-z0-9_-]*/);
    if (!match) {
      throw new MecoError("Invalid nonterminal reference", [
        diagnostic(filename, line, "@ must be followed by a rule name; use @@ for a literal @"),
      ]);
    }

    flushLiteral();
    if (match[0] !== "empty") {
      parts.push({ type: "nonterminal", name: match[0] });
    }
    cursor += match[0].length + 1;
  }

  flushLiteral();
  return parts;
}

export function compile(source, options = {}) {
  const filename = options.filename ?? "<grammar>";
  const rules = new Map();
  const diagnostics = [];
  let version = null;
  let start = null;
  let currentRule = null;

  const lines = source.replaceAll("\r\n", "\n").split("\n");

  for (let index = 0; index < lines.length; index += 1) {
    const lineNumber = index + 1;
    const line = lines[index].trimEnd();
    const trimmed = line.trimStart();

    if (trimmed === "" || trimmed.startsWith("//")) {
      continue;
    }

    const versionMatch = trimmed.match(/^@meco\s+(\d+(?:\.\d+)*)$/);
    if (versionMatch) {
      if (version !== null) {
        diagnostics.push(diagnostic(filename, lineNumber, "@meco may only be declared once"));
      } else {
        version = versionMatch[1];
      }
      currentRule = null;
      continue;
    }

    const startMatch = trimmed.match(/^@start\s+([A-Za-z][A-Za-z0-9_-]*)$/);
    if (startMatch) {
      if (start !== null) {
        diagnostics.push(diagnostic(filename, lineNumber, "@start may only be declared once"));
      } else {
        start = startMatch[1];
      }
      currentRule = null;
      continue;
    }

    const ruleMatch = trimmed.match(/^#\s+(.+?)\s*$/);
    if (ruleMatch) {
      const name = ruleMatch[1];
      if (name === "empty") {
        diagnostics.push(
          diagnostic(filename, lineNumber, "rule name empty is reserved for the @empty built-in"),
        );
        currentRule = null;
      } else if (!RULE_NAME.test(name)) {
        diagnostics.push(
          diagnostic(filename, lineNumber, `invalid rule name ${JSON.stringify(name)}`),
        );
        currentRule = null;
      } else if (rules.has(name)) {
        diagnostics.push(diagnostic(filename, lineNumber, `duplicate rule # ${name}`));
        currentRule = null;
      } else {
        currentRule = { name, line: lineNumber, productions: [] };
        rules.set(name, currentRule);
      }
      continue;
    }

    const productionMatch = trimmed.match(/^-\s?(.*)$/);
    if (productionMatch) {
      if (currentRule === null) {
        diagnostics.push(
          diagnostic(filename, lineNumber, "production appears outside a # rule section"),
        );
        continue;
      }

      let body = productionMatch[1];
      let weight = 1;
      const weightMatch = body.match(/^\[(\d+(?:\.\d+)?)\]\s?(.*)$/);
      if (weightMatch) {
        weight = Number(weightMatch[1]);
        body = weightMatch[2];
      }

      if (!Number.isFinite(weight) || weight <= 0) {
        diagnostics.push(diagnostic(filename, lineNumber, "production weight must be greater than 0"));
        continue;
      }

      try {
        currentRule.productions.push({
          weight,
          parts: parseProduction(body, filename, lineNumber),
          line: lineNumber,
          source: body,
        });
      } catch (error) {
        if (error instanceof MecoError) {
          diagnostics.push(...error.diagnostics);
        } else {
          throw error;
        }
      }
      continue;
    }

    if (trimmed.startsWith("@")) {
      diagnostics.push(diagnostic(filename, lineNumber, `unknown directive ${JSON.stringify(trimmed)}`));
    } else {
      diagnostics.push(
        diagnostic(filename, lineNumber, "expected @meco, @start, a # rule, or a - production"),
      );
    }
  }

  if (version === null) {
    diagnostics.push(diagnostic(filename, 1, "missing @meco version declaration"));
  } else if (version !== "1") {
    diagnostics.push(diagnostic(filename, 1, `unsupported Mecojoni version ${version}`));
  }

  if (start === null) {
    diagnostics.push(diagnostic(filename, 1, "missing @start declaration"));
  } else if (!rules.has(start)) {
    diagnostics.push(diagnostic(filename, 1, `start rule @${start} is not defined`));
  }

  for (const rule of rules.values()) {
    if (rule.productions.length === 0) {
      diagnostics.push(diagnostic(filename, rule.line, `rule # ${rule.name} has no productions`));
    }

    for (const production of rule.productions) {
      for (const part of production.parts) {
        if (part.type === "nonterminal" && !rules.has(part.name)) {
          diagnostics.push(
            diagnostic(
              filename,
              production.line,
              `rule # ${rule.name} references undefined rule @${part.name}`,
            ),
          );
        }
      }
    }
  }

  // A rule is productive if at least one production can eventually become terminals only.
  const productive = new Set();
  let changed = true;
  while (changed) {
    changed = false;
    for (const rule of rules.values()) {
      if (productive.has(rule.name)) continue;
      const canFinish = rule.productions.some((production) =>
        production.parts.every(
          (part) => part.type === "terminal" || productive.has(part.name),
        ),
      );
      if (canFinish) {
        productive.add(rule.name);
        changed = true;
      }
    }
  }

  if (start !== null && rules.has(start)) {
    const reachable = new Set([start]);
    const queue = [start];
    while (queue.length > 0) {
      const name = queue.shift();
      for (const production of rules.get(name).productions) {
        for (const part of production.parts) {
          if (part.type === "nonterminal" && rules.has(part.name) && !reachable.has(part.name)) {
            reachable.add(part.name);
            queue.push(part.name);
          }
        }
      }
    }

    for (const name of reachable) {
      if (!productive.has(name)) {
        diagnostics.push(
          diagnostic(filename, rules.get(name).line, `reachable rule # ${name} can never finish`),
        );
      }
    }
  }

  if (diagnostics.length > 0) {
    throw new MecoError(`Could not compile ${filename}`, diagnostics);
  }

  return Object.freeze({ filename, version, start, rules });
}

function hashSeed(seed) {
  let hash = 0x811c9dc5;
  for (const character of String(seed)) {
    hash ^= character.codePointAt(0);
    hash = Math.imul(hash, 0x01000193);
  }
  return hash >>> 0;
}

function mulberry32(seed) {
  let state = seed >>> 0;
  return () => {
    state = (state + 0x6d2b79f5) >>> 0;
    let value = state;
    value = Math.imul(value ^ (value >>> 15), value | 1);
    value ^= value + Math.imul(value ^ (value >>> 7), value | 61);
    return ((value ^ (value >>> 14)) >>> 0) / 4294967296;
  };
}

function weightedChoice(productions, weights, random) {
  const totalWeight = weights.reduce((sum, weight) => sum + weight, 0);
  const target = random() * totalWeight;
  let cumulative = 0;

  for (let index = 0; index < productions.length; index += 1) {
    cumulative += weights[index];
    if (target < cumulative) {
      return { production: productions[index], index };
    }
  }

  return { production: productions.at(-1), index: productions.length - 1 };
}

function estimateProductionDiversities(grammar, options = {}) {
  const iterations = options.iterations ?? 16;
  const cap = options.cap ?? 1_000_000_000_000;
  let ruleCounts = new Map([...grammar.rules.keys()].map((name) => [name, 1]));

  const productionCount = (production, counts) => {
    let count = 1;
    for (const part of production.parts) {
      if (part.type !== "nonterminal") continue;
      count *= counts.get(part.name) ?? 1;
      if (count >= cap) return cap;
    }
    return Math.max(1, count);
  };

  for (let iteration = 0; iteration < iterations; iteration += 1) {
    const nextCounts = new Map();
    for (const rule of grammar.rules.values()) {
      let count = 0;
      for (const production of rule.productions) {
        count += productionCount(production, ruleCounts);
        if (count >= cap) {
          count = cap;
          break;
        }
      }
      nextCounts.set(rule.name, Math.max(1, count));
    }
    ruleCounts = nextCounts;
  }

  return new Map(
    [...grammar.rules.values()].map((rule) => [
      rule.name,
      rule.productions.map((production) => productionCount(production, ruleCounts)),
    ]),
  );
}

function findRecursiveRules(grammar) {
  const dependencies = new Map(
    [...grammar.rules.values()].map((rule) => [
      rule.name,
      new Set(
        rule.productions.flatMap((production) =>
          production.parts
            .filter((part) => part.type === "nonterminal")
            .map((part) => part.name),
        ),
      ),
    ]),
  );

  const recursive = new Set();
  for (const origin of dependencies.keys()) {
    const pending = [...dependencies.get(origin)];
    const visited = new Set();
    while (pending.length > 0) {
      const current = pending.pop();
      if (current === origin) {
        recursive.add(origin);
        break;
      }
      if (visited.has(current)) continue;
      visited.add(current);
      pending.push(...(dependencies.get(current) ?? []));
    }
  }
  return recursive;
}

function normalizedWords(text) {
  return (text.toLocaleLowerCase().match(/[\p{L}\p{N}]+(?:['’][\p{L}\p{N}]+)*/gu) ?? []);
}

function normalizedSentences(text) {
  return text
    .split(/[.!?]+(?:[”"']+)?(?=\s+|$)/u)
    .map((sentence) => normalizedWords(sentence))
    .filter((words) => words.length > 0);
}

export function edgeFragments(text, options = {}) {
  const minWords = options.minWords ?? 3;
  const maxWords = options.maxWords ?? 8;
  const sentenceMinWords = options.sentenceMinWords ?? 2;
  const words = normalizedWords(text);
  const fragments = [];
  const upperBound = Math.min(maxWords, words.length);

  for (let size = minWords; size <= upperBound; size += 1) {
    const prefix = words.slice(0, size).join(" ");
    const suffix = words.slice(-size).join(" ");
    fragments.push({ kind: "prefix", size, text: prefix, key: `prefix:${size}:${prefix}` });
    fragments.push({ kind: "suffix", size, text: suffix, key: `suffix:${size}:${suffix}` });
  }

  const sentences = normalizedSentences(text);
  for (let index = 0; index < sentences.length; index += 1) {
    const sentenceWords = sentences[index];
    const sentenceUpperBound = Math.min(maxWords, sentenceWords.length);
    for (let size = sentenceMinWords; size <= sentenceUpperBound; size += 1) {
      if (index > 0) {
        const prefix = sentenceWords.slice(0, size).join(" ");
        fragments.push({
          kind: "sentence-prefix",
          size,
          text: prefix,
          key: `sentence-prefix:${size}:${prefix}`,
        });
      }
      if (index < sentences.length - 1) {
        const suffix = sentenceWords.slice(-size).join(" ");
        fragments.push({
          kind: "sentence-suffix",
          size,
          text: suffix,
          key: `sentence-suffix:${size}:${suffix}`,
        });
      }
    }
  }

  const normalized = words.join(" ");
  if (normalized.length > 0) {
    fragments.push({ kind: "exact", size: words.length, text: normalized, key: `exact:${normalized}` });
  }

  return fragments;
}

export class MecoGenerator {
  constructor(grammar, options = {}) {
    this.grammar = grammar;
    this.seed = String(options.seed ?? "mecojoni");
    this.random = mulberry32(hashSeed(this.seed));
    this.maxDepth = options.maxDepth ?? 80;
    this.maxExpansions = options.maxExpansions ?? 2_000;
    this.selection = options.selection ?? "varied";
    if (this.selection !== "varied" && this.selection !== "random") {
      throw new MecoError(`Unknown selection mode ${JSON.stringify(this.selection)}`);
    }

    this.maxRuleCooldown = options.maxRuleCooldown ?? 4;
    this.minimumRuleGap = options.minimumRuleGap ?? 1;
    this.cooldownStrength = options.cooldownStrength ?? 0.75;
    this.diversityStrength = options.diversityStrength ?? 0.5;
    this.maxDiversityBoost = options.maxDiversityBoost ?? 4;
    this.noveltyAttempts = options.noveltyAttempts ?? 12;
    this.noveltyWindow = options.noveltyWindow ?? 300;
    this.exactNoveltyWindow = options.exactNoveltyWindow ?? 50_000;
    this.fragmentMinWords = options.fragmentMinWords ?? 3;
    this.fragmentMaxWords = options.fragmentMaxWords ?? 8;
    this.productionDiversities = estimateProductionDiversities(grammar);
    this.recursiveRules = findRecursiveRules(grammar);
    this.emptyRules = new Set(
      [...grammar.rules.values()]
        .filter((rule) => rule.productions.some((production) => production.parts.length === 0))
        .map((rule) => rule.name),
    );
    this.structuralRules = new Set(
      [...grammar.rules.values()]
        .filter((rule) =>
          rule.productions.some((production) =>
            production.parts.some((part) => part.type === "nonterminal"),
          ),
        )
        .map((rule) => rule.name),
    );
    this.ruleSelectionState = new Map();
    this.recentFragmentCounts = new Map();
    this.recentFragmentQueue = [];
    this.recentExactCounts = new Map();
    this.recentExactQueue = [];
  }

  chooseProduction(rule) {
    const diversity = this.productionDiversities.get(rule.name);
    const recursive = this.recursiveRules.has(rule.name);
    const probabilitySensitive = recursive || this.emptyRules.has(rule.name);
    let weights = rule.productions.map((production, index) => {
      // Recursive branch weights usually encode the grammar author's
      // termination strategy. Empty-production weights similarly encode
      // optionality. Do not distort or cooldown either kind of choice.
      if (this.selection === "random" || probabilitySensitive) return production.weight;
      const descendants = Math.max(1, diversity?.[index] ?? 1);
      const boost = Math.min(
        this.maxDiversityBoost,
        Math.pow(1 + Math.log2(descendants), this.diversityStrength),
      );
      return production.weight * boost;
    });

    if (
      this.selection === "varied" &&
      !probabilitySensitive &&
      this.structuralRules.has(rule.name) &&
      rule.productions.length > 1
    ) {
      let state = this.ruleSelectionState.get(rule.name);
      if (!state) {
        state = { draw: 0, lastUsed: Array(rule.productions.length).fill(null) };
        this.ruleSelectionState.set(rule.name, state);
      }

      state.draw += 1;
      const cooldown = Math.min(this.maxRuleCooldown, rule.productions.length - 1);
      const hardGap = Math.min(this.minimumRuleGap, cooldown);
      weights = weights.map((weight, index) => {
        const lastUsed = state.lastUsed[index];
        if (lastUsed === null || cooldown === 0) return weight;

        const age = state.draw - lastUsed;
        if (age <= hardGap) return 0;
        if (cooldown <= hardGap) return weight;

        // A soft recovery avoids synchronising nested rules into deterministic
        // shuffle cycles while still making recent reuse substantially rarer.
        const recovery = Math.min(1, (age - hardGap) / (cooldown - hardGap + 1));
        return weight * Math.pow(recovery, this.cooldownStrength);
      });

      const choice = weightedChoice(rule.productions, weights, this.random);
      state.lastUsed[choice.index] = state.draw;
      return choice;
    }

    return weightedChoice(rule.productions, weights, this.random);
  }

  expandOnce(start) {
    let expansions = 0;
    const trace = [];

    const expand = (name, depth) => {
      if (depth > this.maxDepth) {
        throw new MecoError(
          `Generation exceeded maximum depth ${this.maxDepth} while expanding @${name}`,
        );
      }
      expansions += 1;
      if (expansions > this.maxExpansions) {
        throw new MecoError(`Generation exceeded ${this.maxExpansions} rule expansions`);
      }

      const rule = this.grammar.rules.get(name);
      const { production, index } = this.chooseProduction(rule);
      trace.push({ rule: name, production: index + 1, line: production.line, depth });

      let text = "";
      for (const part of production.parts) {
        text += part.type === "terminal" ? part.value : expand(part.name, depth + 1);
      }
      return text;
    };

    return { text: expand(start, 0), trace };
  }

  candidateFragments(text) {
    return edgeFragments(text, {
      minWords: this.fragmentMinWords,
      maxWords: this.fragmentMaxWords,
    });
  }

  noveltyScore(text) {
    let exactScore = 0;
    let prefixScore = 0;
    let suffixScore = 0;
    for (const fragment of this.candidateFragments(text)) {
      if (fragment.kind === "exact") {
        exactScore += (this.recentExactCounts.get(fragment.key) ?? 0) * 1_000_000;
        continue;
      }

      const repetitions = this.recentFragmentCounts.get(fragment.key) ?? 0;
      const lengthWeight = 1 + (fragment.size - this.fragmentMinWords) * 0.05;
      const fragmentScore = repetitions * lengthWeight;
      if (fragment.kind.endsWith("prefix")) {
        prefixScore = Math.max(prefixScore, fragmentScore);
      }
      else suffixScore = Math.max(suffixScore, fragmentScore);
    }
    return exactScore + prefixScore + suffixScore;
  }

  rememberFragments(text) {
    const fragments = this.candidateFragments(text);
    const keys = [
      ...new Set(
        fragments
          .filter((fragment) => fragment.kind !== "exact")
          .map((fragment) => fragment.key),
      ),
    ];
    this.recentFragmentQueue.push(keys);
    for (const key of keys) {
      this.recentFragmentCounts.set(key, (this.recentFragmentCounts.get(key) ?? 0) + 1);
    }

    while (this.recentFragmentQueue.length > this.noveltyWindow) {
      for (const key of this.recentFragmentQueue.shift()) {
        const count = (this.recentFragmentCounts.get(key) ?? 1) - 1;
        if (count === 0) this.recentFragmentCounts.delete(key);
        else this.recentFragmentCounts.set(key, count);
      }
    }

    const exact = fragments.find((fragment) => fragment.kind === "exact");
    if (exact) {
      this.recentExactQueue.push(exact.key);
      this.recentExactCounts.set(exact.key, (this.recentExactCounts.get(exact.key) ?? 0) + 1);
    }
    while (this.recentExactQueue.length > this.exactNoveltyWindow) {
      const key = this.recentExactQueue.shift();
      const count = (this.recentExactCounts.get(key) ?? 1) - 1;
      if (count === 0) this.recentExactCounts.delete(key);
      else this.recentExactCounts.set(key, count);
    }
  }

  generate(options = {}) {
    const start = options.start ?? this.grammar.start;
    if (!this.grammar.rules.has(start)) {
      throw new MecoError(`Unknown start rule @${start}`);
    }

    const maxAttempts = this.selection === "varied" ? this.noveltyAttempts : 1;
    let result = null;
    let bestScore = Number.POSITIVE_INFINITY;
    let attempts = 0;
    for (let attempt = 1; attempt <= maxAttempts; attempt += 1) {
      const candidate = this.expandOnce(start);
      const score = this.selection === "varied" ? this.noveltyScore(candidate.text) : 0;
      attempts = attempt;
      if (score < bestScore) {
        result = candidate;
        bestScore = score;
      }
      if (score === 0) break;
    }

    result.noveltyAttempts = attempts;
    result.noveltyScore = bestScore;
    if (this.selection === "varied") {
      this.rememberFragments(result.text);
    }
    return result;
  }
}

export function generate(grammar, options = {}) {
  return new MecoGenerator(grammar, options).generate(options);
}
