#!/usr/bin/env node

import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { pathToFileURL } from 'node:url';

/** Returns all records in one batch ledger that belong to one example. */
function recordsForCase(records, caseId) {
  return Array.isArray(records) ? records.filter((record) => record.caseId === caseId) : [];
}

/** Extracts one independently verifiable strict report from a closed batch ledger. */
export function splitCaseReport(batch, caseId) {
  const quadrants = recordsForCase(batch.quadrants, caseId);
  const crossComparisons = recordsForCase(batch.crossComparisons, caseId);
  const stabilityComparisons = recordsForCase(batch.stabilityComparisons, caseId);
  const generatedArtifacts = Array.isArray(batch.generatedArtifacts)
    ? batch.generatedArtifacts
    : [];
  const failures = [
    ...quadrants.flatMap((record) => record.failures ?? []),
    ...crossComparisons.flatMap((record) => record.failures ?? []),
    ...stabilityComparisons.flatMap((record) => record.failures ?? [])
  ];
  const expectedRunCount = quadrants.length;
  const status = batch.status !== 'pass'
    ? (quadrants.length > 0
      && quadrants.every((record) => record.status === 'pass')
      && crossComparisons.every((record) => record.status === 'pass')
      && stabilityComparisons.every((record) => record.status === 'pass')
      && generatedArtifacts.every((record) => record.status === 'pass')
      && batch.gpuBoundaryLint?.status === 'pass'
      && failures.length === 0
      ? 'pass' : 'fail')
    : 'pass';
  return {
    schemaVersion: 1,
    evidenceType: 'strict-example-case',
    gate: batch.gate ?? 'three-r185-single-case-four-quadrant-three-repeat',
    caseId,
    status,
    selectedCases: [caseId],
    repeatCount: batch.repeatCount,
    runCount: quadrants.length,
    expectedRunCount,
    pipelines: batch.pipelines,
    backends: batch.backends,
    quadrants,
    crossComparisons,
    stabilityComparisons,
    generatedArtifacts,
    gpuBoundaryLint: batch.gpuBoundaryLint,
    thresholds: batch.thresholds,
    failures
  };
}

/** Writes one report per case without mutating the aggregate batch ledger. */
async function main() {
  const batchIndex = process.argv.indexOf('--batch');
  const outputIndex = process.argv.indexOf('--output-dir');
  if (batchIndex < 0 || outputIndex < 0
    || process.argv[batchIndex + 1] == null || process.argv[outputIndex + 1] == null) {
    throw new Error('Usage: split_three_batch_reports.mjs --batch <summary.json> --output-dir <directory> [--case-ids id1,id2]');
  }
  const batchPath = path.resolve(process.argv[batchIndex + 1]);
  const outputDirectory = path.resolve(process.argv[outputIndex + 1]);
  const caseIndex = process.argv.indexOf('--case-ids');
  const batch = JSON.parse(await fs.readFile(batchPath, 'utf8'));
  const caseIds = caseIndex >= 0
    ? process.argv[caseIndex + 1].split(',').filter(Boolean)
    : [...new Set((batch.quadrants ?? []).map((record) => record.caseId))];
  const results = [];
  for (const caseId of caseIds) {
    const report = splitCaseReport(batch, caseId);
    const reportPath = path.join(outputDirectory, caseId, 'summary.json');
    await fs.mkdir(path.dirname(reportPath), { recursive: true });
    await fs.writeFile(reportPath, `${JSON.stringify(report, null, 2)}\n`, 'utf8');
    results.push({ caseId, status: report.status, reportPath });
  }
  console.log(JSON.stringify({ batch: batchPath, results }, null, 2));
  if (results.some((result) => result.status !== 'pass')) process.exitCode = 1;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
