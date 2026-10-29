#!/usr/bin/env node

import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

const repositoryRoot = path.resolve(new URL('../..', import.meta.url).pathname);

/** Reads one explicit JSON document. */
async function readJson(value) {
  const filePath = path.isAbsolute(value) ? value : path.resolve(repositoryRoot, value);
  return JSON.parse(await fs.readFile(filePath, 'utf8'));
}

/**
 * Attaches the latest batch evidence to a queue without promoting any case to
 * strict-pass.  This keeps execution history visible while preserving the
 * atomic publication gate.
 */
export function annotateThreeBatchQueue(queue, summary, summaryPath) {
  const activeCases = Array.isArray(queue?.activeCases) ? queue.activeCases : [];
  const results = new Map((summary?.caseResults ?? []).map((result) => [result.caseId, result]));
  if (activeCases.length !== 20) throw new Error('A queue annotation requires exactly 20 active cases.');
  const missing = activeCases.filter((entry) => !results.has(entry.caseId)).map((entry) => entry.caseId);
  if (missing.length > 0) throw new Error(`Batch summary is missing queue cases: ${missing.join(', ')}.`);
  return {
    ...queue,
    lastRun: {
      summaryPath,
      status: summary.status ?? 'fail',
      generatedAt: summary.generatedAt ?? null,
      repeatCount: summary.repeatCount ?? null,
      totals: summary.totals ?? null,
      sourceLint: summary.singleSampleSourceLint?.status ?? null,
      gpuBoundaryLint: summary.gpuBoundaryLint?.status ?? null,
      generatedArtifactLint: summary.generatedArtifactLint?.status ?? null
    },
    activeCases: activeCases.map((entry) => {
      const result = results.get(entry.caseId);
      return {
        ...entry,
        lastRun: {
          status: result.status,
          reportPath: result.reportPath ?? null,
          scenarioCount: result.scenarioCount ?? 0,
          quadrantCount: result.quadrantCount ?? 0,
          crossComparisonCount: result.crossComparisonCount ?? 0,
          stabilityComparisonCount: result.stabilityComparisonCount ?? 0,
          failures: (result.failures ?? []).slice(0, 4)
        }
      };
    })
  };
}

/** Updates one queue file with an explicit latest-run summary. */
async function main() {
  const args = process.argv.slice(2);
  const options = {};
  for (let index = 0; index < args.length; index += 2) {
    const option = args[index];
    const value = args[index + 1];
    if (!option?.startsWith('--') || value == null || value.startsWith('--')) {
      throw new Error(`Expected --option value near '${option ?? '<end>'}'.`);
    }
    options[option.slice(2)] = value;
  }
  if (!options.queue || !options.summary || !options.output) {
    throw new Error('Usage: annotate_three_batch_queue.mjs --queue <path> --summary <path> --output <path>');
  }
  const queue = await readJson(options.queue);
  const summary = await readJson(options.summary);
  const output = annotateThreeBatchQueue(queue, summary, options.summary);
  const outputPath = path.isAbsolute(options.output) ? options.output : path.resolve(repositoryRoot, options.output);
  await fs.writeFile(outputPath, `${JSON.stringify(output, null, 2)}\n`, 'utf8');
  process.stdout.write(`${JSON.stringify({
    output: path.relative(repositoryRoot, outputPath),
    status: output.lastRun.status,
    caseCount: output.activeCases.length,
    strictPassCount: output.activeCases.filter((entry) => entry.lastRun.status === 'pass').length
  }, null, 2)}\n`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
