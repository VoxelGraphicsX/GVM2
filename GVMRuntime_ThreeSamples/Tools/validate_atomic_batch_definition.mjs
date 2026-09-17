#!/usr/bin/env node

import { createHash } from 'node:crypto';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath, pathToFileURL } from 'node:url';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../..');
const sampleRoot = path.join(repositoryRoot, 'GVMRuntime_ThreeSamples');

/** Computes the stable identity of one sorted example identifier inventory. */
export function computeCaseIdSha256(caseIds) {
  return createHash('sha256')
    .update(`${[...caseIds].sort().join('\n')}\n`)
    .digest('hex');
}

/** Validates one atomic batch against its immutable strict baseline and r185 manifest. */
export function validateAtomicBatchDefinition(batch, baseline, manifest) {
  const failures = [];
  const cases = Array.isArray(batch?.cases) ? batch.cases : [];
  const caseIds = cases.map((entry) => entry?.caseId);
  const baselineIds = Array.isArray(baseline?.strictCaseIds)
    ? baseline.strictCaseIds
    : [];
  const baselineSet = new Set(baselineIds);
  const manifestById = new Map(
    (manifest?.examples ?? []).map((example) => [example.id, example]));

  if (batch?.publicationMode !== 'atomic') {
    failures.push('Batch publicationMode must be atomic.');
  }
  if (batch?.minimumCaseCount !== 20 || batch?.minimumNetNewCaseCount !== 20
    || cases.length !== 20) {
    failures.push('Batch must contain exactly 20 net-new active cases.');
  }
  if (new Set(caseIds).size !== caseIds.length) {
    failures.push('Batch case identifiers must be unique.');
  }
  if (baseline?.strictCaseCount !== baselineIds.length
    || batch?.baselineStrictCaseCount !== baselineIds.length) {
    failures.push('Baseline strict case count does not match its inventory.');
  }
  const baselineSha256 = computeCaseIdSha256(baselineIds);
  if (baseline?.strictCaseIdsSha256 !== baselineSha256
    || batch?.baselineStrictCaseIdsSha256 !== baselineSha256) {
    failures.push('Baseline strict case SHA-256 does not match its inventory.');
  }
  for (const caseId of caseIds) {
    if (typeof caseId !== 'string' || !manifestById.has(caseId)) {
      failures.push(`Batch case '${String(caseId)}' is absent from the r185 manifest.`);
      continue;
    }
    if (baselineSet.has(caseId)) {
      failures.push(`Batch case '${caseId}' is not net-new.`);
    }
    if (manifestById.get(caseId).status !== 'phase1_required') {
      failures.push(`Batch case '${caseId}' is not phase1_required.`);
    }
  }
  for (const entry of cases) {
    if (!['scaffolded', 'semantic-complete', 'strict-pass'].includes(
      entry?.implementationLevel)) {
      failures.push(
        `Batch case '${String(entry?.caseId)}' has an invalid implementationLevel.`);
    }
    if (entry?.implementationLevel === 'strict-pass'
      && (typeof entry.reportPath !== 'string' || entry.reportPath.length === 0)) {
      failures.push(
        `Strict batch case '${String(entry?.caseId)}' must declare reportPath.`);
    }
    if (Object.hasOwn(entry ?? {}, 'strictReport')) {
      failures.push(
        `Batch case '${String(entry?.caseId)}' uses obsolete strictReport; use reportPath.`);
    }
  }
  const msaaPolicy = batch?.upstreamMsaaPolicy;
  if (msaaPolicy?.mode !== 'single-sample-rendering'
    || msaaPolicy.excludeExamples !== false
    || msaaPolicy.simulateMsaa !== false
    || msaaPolicy.requireMsaaParity !== false) {
    failures.push('Batch must keep MSAA examples in scope and render them without simulation.');
  }
  return failures;
}

/** Parses explicit command-line path options without environment fallbacks. */
export function parseArguments(argv) {
  const options = {};
  for (let index = 2; index < argv.length; index += 2) {
    const option = argv[index];
    const value = argv[index + 1];
    if (!option?.startsWith('--') || value == null || value.startsWith('--')) {
      throw new Error(`Expected --option value pair near '${option ?? '<end>'}'.`);
    }
    const name = option.slice(2);
    if (Object.hasOwn(options, name)) {
      throw new Error(`Duplicate --${name}.`);
    }
    options[name] = value;
  }
  return options;
}

/** Resolves a repository-relative path supplied by the caller. */
function resolveRepositoryPath(value, fallback) {
  return path.resolve(repositoryRoot, value ?? fallback);
}

/** Loads a baseline embedded in a batch or referenced by its path. */
async function loadBatchBaseline(batch, batchPath) {
  if (typeof batch?.baselineStrictCaseIdsPath === 'string') {
    const candidatePaths = [
      path.resolve(sampleRoot, batch.baselineStrictCaseIdsPath),
      path.resolve(repositoryRoot, batch.baselineStrictCaseIdsPath),
      path.resolve(path.dirname(batchPath), batch.baselineStrictCaseIdsPath)
    ];
    let baselinePath = candidatePaths.at(-1);
    for (const candidatePath of candidatePaths) {
      try {
        await fs.access(candidatePath);
        baselinePath = candidatePath;
        break;
      } catch {
        // Try the next repository-relative convention.
      }
    }
    return JSON.parse(await fs.readFile(baselinePath, 'utf8'));
  }
  const strictCaseIds = Array.isArray(batch?.baselineStrictCaseIds)
    ? batch.baselineStrictCaseIds : [];
  return {
    strictCaseCount: strictCaseIds.length,
    strictCaseIds,
    strictCaseIdsSha256: batch?.baselineStrictCaseIdsSha256
      ?? computeCaseIdSha256(strictCaseIds)
  };
}

/** Loads and validates one explicitly selected atomic batch definition. */
async function main() {
  const options = parseArguments(process.argv);
  const batchPath = resolveRepositoryPath(
    options.batch, 'GVMRuntime_ThreeSamples/Manifest/three-r185-batch20-wave3.json');
  const batch = JSON.parse(await fs.readFile(batchPath, 'utf8'));
  const manifestPath = resolveRepositoryPath(
    options.manifest, 'GVMRuntime_ThreeSamples/Manifest/three-r185-manifest.json');
  const baseline = await loadBatchBaseline(batch, batchPath);
  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  const failures = validateAtomicBatchDefinition(batch, baseline, manifest);
  if (failures.length > 0) {
    throw new Error(failures.join('\n'));
  }
  process.stdout.write(
    `Validated ${batch.cases.length} net-new cases against ${baseline.strictCaseCount} strict baseline cases.\n`);
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  await main();
}
