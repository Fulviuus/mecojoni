// Timing-only compatibility baseline for the archived JavaScript v1 engine.
// v1 and v2 intentionally have different PRNG/session contracts; this records
// comparable source shapes and operation counts, not output identity.
import { compile, MecoGenerator } from "../v1/src/meco.js";
import { workloads, workloadVersion } from "./workloads.ts";

const encoder = new TextEncoder();
const samples = 5;

function v1Source(source: string): string {
  const end = source.indexOf("---\n\n", 4);
  if (end < 0) throw new Error("v2 workload header is malformed");
  return `@meco 1\n@start root\n\n${source.slice(end + 5)}`;
}

function median(values: number[]): number {
  return values.toSorted((left, right) => left - right)[Math.floor(values.length / 2)];
}

for (const workload of workloads()) {
  const source = v1Source(workload.source);
  const compileMs: number[] = [];
  const generationMs: number[] = [];
  let expansions = 0;
  let outputBytes = 0;
  for (let sample = 0; sample < samples; sample++) {
    const compileStarted = performance.now();
    const grammar = compile(source, { filename: `${workload.name}.meco` });
    const generator = new MecoGenerator(grammar, {
      seed: "0",
      maxDepth: 2_048,
      maxExpansions: 100_000,
      selection: "random",
    });
    compileMs.push(performance.now() - compileStarted);
    const generationStarted = performance.now();
    let sampleExpansions = 0;
    let sampleOutputBytes = 0;
    for (let generation = 0; generation < workload.generations; generation++) {
      const result = generator.generate();
      if (result === null) throw new Error(`${workload.name}: v1 generation returned no result`);
      sampleExpansions += result.trace.length;
      sampleOutputBytes += encoder.encode(result.text).byteLength;
    }
    generationMs.push(performance.now() - generationStarted);
    if (sample === 0) {
      expansions = sampleExpansions;
      outputBytes = sampleOutputBytes;
    }
  }
  console.log(JSON.stringify({
    engine: "v1-js",
    version: workloadVersion,
    scenario: workload.name,
    class: workload.class,
    samples,
    sourceBytes: encoder.encode(source).byteLength,
    generations: workload.generations,
    compileMsMedian: median(compileMs),
    generationMsMedian: median(generationMs),
    expansions,
    outputBytes,
    contract: "v1 random session; timing comparison only",
  }));
}
