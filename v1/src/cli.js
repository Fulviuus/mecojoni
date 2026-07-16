#!/usr/bin/env node

import { readFile } from "node:fs/promises";
import process from "node:process";
import {
  auditComposition,
  auditGrammar,
  formatAudit,
  formatCompositionAudit,
} from "./audit.js";
import { compile, MecoError, MecoGenerator } from "./meco.js";

function usage() {
  return `Usage: meco <grammar.meco> [options]

Options:
  -n, --count <number>  Number of phrases to generate (default: 1)
  --seed <value>        Reproduce a sequence with a specific seed
  --start <rule>        Generate from a rule other than @start
  --selection <mode>    varied (default) or random
  --audit <samples>     Report repeated openings, endings, and exact lines
  --audit-composition   Report fixed sentence shells reachable from @start
  --trace               Print the chosen derivation rules
  -h, --help            Show this help`;
}

function parseArguments(argv) {
  const options = {
    count: 1,
    seed: String(Date.now()),
    start: undefined,
    trace: false,
    selection: "varied",
    audit: null,
    auditComposition: false,
  };
  let file = null;

  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    if (argument === "-h" || argument === "--help") {
      options.help = true;
    } else if (argument === "-n" || argument === "--count") {
      options.count = Number(argv[++index]);
    } else if (argument === "--seed") {
      options.seed = argv[++index];
    } else if (argument === "--start") {
      options.start = argv[++index];
    } else if (argument === "--selection") {
      options.selection = argv[++index];
    } else if (argument === "--audit") {
      options.audit = Number(argv[++index]);
    } else if (argument === "--audit-composition") {
      options.auditComposition = true;
    } else if (argument === "--trace") {
      options.trace = true;
    } else if (argument.startsWith("-")) {
      throw new MecoError(`Unknown option ${argument}`);
    } else if (file === null) {
      file = argument;
    } else {
      throw new MecoError(`Unexpected argument ${argument}`);
    }
  }

  if (!Number.isSafeInteger(options.count) || options.count < 1) {
    throw new MecoError("--count must be a positive integer");
  }
  if (options.seed === undefined) {
    throw new MecoError("--seed requires a value");
  }
  if (options.start === undefined && argv.includes("--start")) {
    throw new MecoError("--start requires a rule name");
  }
  if (options.selection !== "varied" && options.selection !== "random") {
    throw new MecoError("--selection must be varied or random");
  }
  if (options.audit !== null && (!Number.isSafeInteger(options.audit) || options.audit < 1)) {
    throw new MecoError("--audit must be a positive integer");
  }

  return { file, options };
}

async function main() {
  const { file, options } = parseArguments(process.argv.slice(2));
  if (options.help || file === null) {
    console.log(usage());
    process.exitCode = options.help ? 0 : 1;
    return;
  }

  const source = await readFile(file, "utf8");
  const grammar = compile(source, { filename: file });
  if (options.auditComposition) {
    console.log(formatCompositionAudit(auditComposition(grammar, { start: options.start })));
    return;
  }
  if (options.audit !== null) {
    const report = auditGrammar(grammar, {
      samples: options.audit,
      seed: options.seed,
      selection: options.selection,
      start: options.start,
    });
    console.log(formatAudit(report));
    return;
  }

  const generator = new MecoGenerator(grammar, {
    seed: options.seed,
    selection: options.selection,
  });

  for (let index = 0; index < options.count; index += 1) {
    const result = generator.generate({ start: options.start });
    console.log(result.text);

    if (options.trace) {
      console.error(`Trace ${index + 1} (novelty attempts: ${result.noveltyAttempts}):`);
      for (const step of result.trace) {
        const indent = "  ".repeat(step.depth);
        console.error(`${indent}@${step.rule} -> production ${step.production} (line ${step.line})`);
      }
    }
  }
}

main().catch((error) => {
  if (error instanceof MecoError) {
    console.error(error.message);
    for (const item of error.diagnostics) {
      console.error(`  ${item}`);
    }
  } else {
    console.error(error instanceof Error ? error.message : String(error));
  }
  process.exitCode = 1;
});
