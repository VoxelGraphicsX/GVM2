#!/usr/bin/env node

import { createHash } from 'node:crypto';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';

/** Reads a required JSON file and rejects malformed or missing batch inputs. */
async function readJson(filePath, label) {
  let text;
  try {
    text = await fs.readFile(filePath, 'utf8');
  } catch (error) {
    throw new Error(`Unable to read ${label} '${filePath}': ${error.message}`);
  }
  try {
    return JSON.parse(text);
  } catch (error) {
    throw new Error(`Invalid JSON in ${label} '${filePath}': ${error.message}`);
  }
}

/** Computes the SHA-256 identity used to bind a batch to one manifest revision. */
async function sha256File(filePath) {
  const bytes = await fs.readFile(filePath);
  return createHash('sha256').update(bytes).digest('hex');
}

/** Parses explicit long-form command-line options without environment fallbacks. */
function parseArguments(argv) {
  const options = {};
  for (let index = 2; index < argv.length; index += 2) {
    const option = argv[index];
    const value = argv[index + 1];
    if (!option?.startsWith('--') || value == null || value.startsWith('--')) {
      throw new Error(`Expected '--option value' near '${option ?? '<end>'}'.`);
    }
    const name = option.slice(2);
    if (Object.hasOwn(options, name)) throw new Error(`Duplicate --${name}.`);
    options[name] = value;
  }
  return options;
}

/** Requires one absolute path option. */
function requiredPath(options, name) {
  if (!options[name]) throw new Error(`Missing required --${name} path.`);
  return path.resolve(options[name]);
}

/** Returns a non-empty list or throws a contract error. */
function requiredArray(value, label) {
  if (!Array.isArray(value) || value.length === 0) {
    throw new Error(`${label} must be a non-empty array.`);
  }
  return value;
}

/** Returns whether an example requires excluded external video/camera/WebXR input. */
function usesExcludedExternalInput(upstreamText) {
  return /video|webxr|getusermedia|mediastream|camera[_-](?:input|capture|device|video)|timing/u.test(upstreamText);
}

/** Reads the implementation inventory entry for one example identifier. */
async function findInventoryEntry(dslRoot, caseId) {
  const directories = await fs.readdir(dslRoot, { withFileTypes: true });
  for (const directory of directories) {
    if (!directory.isDirectory()) continue;
    const inventoryPath = path.join(dslRoot, directory.name, 'implemented-cases.json');
    try {
      await fs.access(inventoryPath);
      const inventory = await readJson(inventoryPath, 'implementation inventory');
      const entry = (inventory.cases ?? []).find((candidate) => candidate.caseId === caseId);
      if (entry) return { entry, inventoryPath };
    } catch (error) {
      if (error.code === 'ENOENT') continue;
      throw error;
    }
  }
  return null;
}

/** Loads the strict baseline from inline IDs or the legacy path spelling. */
async function loadBaselineCaseIds(batch, batchPath) {
  if (Array.isArray(batch?.baselineStrictCaseIds)) {
    return new Set(batch.baselineStrictCaseIds);
  }
  const reference = batch?.baselineStrictCaseIdsSource
    ?? batch?.baselineStrictCaseIdsPath;
  if (typeof reference !== 'string' || reference.length === 0) {
    return new Set();
  }
  const candidatePaths = [
    path.resolve(reference),
    path.resolve(path.dirname(batchPath), reference),
    path.resolve(path.dirname(batchPath), '..', reference)
  ];
  for (const candidatePath of candidatePaths) {
    try {
      await fs.access(candidatePath);
    } catch (error) {
      if (error.code === 'ENOENT') continue;
      throw error;
    }
    const baseline = await readJson(candidatePath, 'baseline strict ledger');
    if (!Array.isArray(baseline.strictCaseIds)) {
      throw new Error(`Baseline '${candidatePath}' does not contain strictCaseIds.`);
    }
    return new Set(baseline.strictCaseIds);
  }
  throw new Error(`Unable to resolve baseline strict ledger '${reference}'.`);
}

/** Validates one strict case result and its required evidence cardinalities. */
function validateCaseResult(caseResult, baselineIds, batchSummary) {
  const failures = [];
  if (baselineIds.has(caseResult.caseId)) failures.push('case is already in the baseline strict ledger');
  if (caseResult.status !== 'pass') failures.push(`status is '${caseResult.status}', expected 'pass'`);
  if (!Number.isInteger(caseResult.scenarioCount) || caseResult.scenarioCount < 1) {
    failures.push('scenarioCount must be a positive integer');
  }
  const scenarioCount = caseResult.scenarioCount ?? 0;
  if (caseResult.quadrantCount !== scenarioCount * 12) {
    failures.push(`quadrantCount must equal scenarioCount*12 (${scenarioCount * 12})`);
  }
  if (caseResult.crossComparisonCount < scenarioCount * 12) {
    failures.push(`crossComparisonCount must be at least ${scenarioCount * 12}`);
  }
  if (caseResult.stabilityComparisonCount !== scenarioCount * 8) {
    failures.push(`stabilityComparisonCount must equal scenarioCount*8 (${scenarioCount * 8})`);
  }
  if (!Array.isArray(caseResult.failures) || caseResult.failures.length !== 0) {
    failures.push('case failures must be an empty array');
  }
  const aggregateStrictEvidence = batchSummary?.evidenceType === 'strict-example-batch';
  if (aggregateStrictEvidence) {
    if (batchSummary.status !== 'pass'
      || batchSummary.singleSampleSourceLint?.status !== 'pass'
      || batchSummary.gpuBoundaryLint?.status !== 'pass'
      || (batchSummary.generatedArtifactLint != null
        && batchSummary.generatedArtifactLint.status !== 'pass')
      || !Array.isArray(batchSummary.totalFailures)
      || batchSummary.totalFailures.length !== 0) {
      failures.push('aggregate strict summary does not prove the single-sample and GPU-boundary gates');
    }
  } else {
    if (caseResult.samplePolicy?.mode !== 'single-sample'
      || caseResult.samplePolicy?.msaaEnabled !== false
      || caseResult.samplePolicy?.simulateMsaa !== false) {
      failures.push('samplePolicy must explicitly disable MSAA and simulation');
    }
    if (!Array.isArray(caseResult.quadrants) || caseResult.quadrants.length !== scenarioCount * 12) {
      failures.push('quadrants must contain all four pipeline/backend combinations for three repetitions');
    }
  }
  return failures;
}

/** Validates one 20-case atomic batch and returns a machine-readable result. */
export async function validateThreeBatch({ manifestPath, batchPath, summaryPath, dslRoot }) {
  const [manifest, batch, summary] = await Promise.all([
    readJson(manifestPath, 'manifest'),
    readJson(batchPath, 'batch manifest'),
    readJson(summaryPath, 'batch summary')
  ]);
  const failures = [];
  const manifestSha256 = await sha256File(manifestPath);
  if (batch.manifestSha256 !== manifestSha256) {
    failures.push(`manifest SHA-256 mismatch: batch=${batch.manifestSha256 ?? '<missing>'} actual=${manifestSha256}`);
  }
  const activeCases = requiredArray(batch.activeCases, 'activeCases');
  const minimumNetNewCaseCount = Number.isInteger(batch.minimumNetNewCaseCount)
    ? batch.minimumNetNewCaseCount
    : 20;
  if (minimumNetNewCaseCount < 20) {
    failures.push('minimumNetNewCaseCount must be at least 20.');
  }
  if (activeCases.length < minimumNetNewCaseCount) {
    failures.push(`activeCases must contain at least ${minimumNetNewCaseCount} examples.`);
  }
  const uniqueIds = new Set(activeCases.map((entry) => entry.caseId));
  if (uniqueIds.size !== activeCases.length) failures.push('activeCases contains duplicate case IDs.');
  const manifestById = new Map((manifest.examples ?? []).map((example) => [example.id, example]));
  const baselineIds = await loadBaselineCaseIds(batch, batchPath);
  if (Number.isInteger(batch.baselineStrictCaseCount)
    && batch.baselineStrictCaseCount !== baselineIds.size) {
    failures.push(
      `baselineStrictCaseCount ${batch.baselineStrictCaseCount} does not match the strict ledger size ${baselineIds.size}.`
    );
  }
  const activeBaselineIds = [...uniqueIds].filter((caseId) => baselineIds.has(caseId));
  if (activeBaselineIds.length > 0) {
    failures.push(
      `activeCases must be net-new; baseline overlap: ${activeBaselineIds.join(', ')}.`
    );
  }
  if (Array.isArray(batch.cases)) {
    const caseIds = batch.cases.map((entry) => entry.caseId);
    if (new Set(caseIds).size !== caseIds.length) {
      failures.push('cases contains duplicate case IDs.');
    }
    if (caseIds.length !== activeCases.length
      || caseIds.some((caseId, index) => caseId !== activeCases[index].caseId)) {
      failures.push('cases must be the same ordered selection as activeCases.');
    }
  }
  if (Number.isInteger(summary.caseCount) && summary.caseCount !== activeCases.length) {
    failures.push(
      `summary.caseCount ${summary.caseCount} does not match activeCases ${activeCases.length}.`
    );
  }
  if (Number.isInteger(summary.baselineCaseCount)
    && summary.baselineCaseCount !== baselineIds.size) {
    failures.push(
      `summary.baselineCaseCount ${summary.baselineCaseCount} does not match the strict ledger size ${baselineIds.size}.`
    );
  }
  if (Number.isInteger(summary.netNewCaseCount)
    && summary.netNewCaseCount !== activeCases.length) {
    failures.push(
      `summary.netNewCaseCount ${summary.netNewCaseCount} does not match activeCases ${activeCases.length}.`
    );
  }
  const caseResults = new Map((summary.caseResults ?? []).map((entry) => [entry.caseId, entry]));
  for (const active of activeCases) {
    const example = manifestById.get(active.caseId);
    if (!example) {
      failures.push(`${active.caseId}: not found in manifest`);
      continue;
    }
    if (example.status !== 'phase1_required') {
      failures.push(`${active.caseId}: manifest status must be phase1_required`);
    }
    const upstreamText = `${example.id} ${example.upstreamPath ?? ''}`.toLowerCase();
    const capabilityText = (example.capabilityAudit?.requiredCapabilities ?? []).join(' ').toLowerCase();
    if (batch.selectionPolicy?.excludeVideoCameraWebxrTiming
      && usesExcludedExternalInput(upstreamText)) {
      failures.push(`${active.caseId}: selection policy excludes video/camera/WebXR/timing examples`);
    }
    if (batch.selectionPolicy?.msaaEnabled !== false || batch.selectionPolicy?.simulateMsaa !== false) {
      failures.push(`${active.caseId}: batch selection policy must freeze msaaEnabled=false and simulateMsaa=false`);
    }
    if (capabilityText.includes('temporal') || capabilityText.includes('occlusion_query')) {
      failures.push(`${active.caseId}: temporal/query capability is not eligible for this frozen batch`);
    }
    if (baselineIds.has(active.caseId)) failures.push(`${active.caseId}: already belongs to baseline strict set`);
    const inventory = await findInventoryEntry(dslRoot, active.caseId);
    if (!inventory) {
      failures.push(`${active.caseId}: no implemented-cases.json entry`);
    } else {
      // Schema-1 manifests often carry only caseId/reportPath while the
      // immutable implementation inventory owns the DSL/Host identity. Use
      // that declaration as the source of truth and reject a mismatch only
      // when the batch explicitly overrides it.
      const dslEntry = active.dslEntry ?? inventory.entry.dslEntry;
      const hostTarget = active.hostTarget ?? inventory.entry.hostTarget;
      if (!dslEntry || !hostTarget) {
        failures.push(`${active.caseId}: missing dedicated DSL/host target`);
      }
      if (active.dslEntry != null && inventory.entry.dslEntry !== active.dslEntry) {
        failures.push(`${active.caseId}: DSL entry differs from batch manifest`);
      }
      if (active.hostTarget != null && inventory.entry.hostTarget !== active.hostTarget) {
        failures.push(`${active.caseId}: host target differs from batch manifest`);
      }
      if (inventory.entry.implementationLevel === 'scaffolded') {
        failures.push(`${active.caseId}: scaffolded implementation is forbidden`);
      }
    }
    const result = caseResults.get(active.caseId);
    if (!result) failures.push(`${active.caseId}: missing summary.caseResults entry`);
    else failures.push(...validateCaseResult(result, baselineIds, summary).map((failure) => `${active.caseId}: ${failure}`));
  }
  for (const result of summary.caseResults ?? []) {
    if (!uniqueIds.has(result.caseId)) failures.push(`${result.caseId}: summary contains an unselected case`);
  }
  if (summary.status !== 'pass') failures.push(`batch summary status is '${summary.status}', expected 'pass'`);
  const result = {
    schemaVersion: 1,
    gate: 'three-r185-strict-atomic-batch',
    status: failures.length === 0 ? 'pass' : 'fail',
    manifestSha256,
    activeCaseCount: activeCases.length,
    caseResults: [...caseResults.keys()],
    failures
  };
  return result;
}

/** Runs the validator CLI and writes a deterministic JSON result. */
async function main() {
  const options = parseArguments(process.argv);
  const result = await validateThreeBatch({
    manifestPath: requiredPath(options, 'manifest'),
    batchPath: requiredPath(options, 'batch'),
    summaryPath: requiredPath(options, 'summary'),
    dslRoot: requiredPath(options, 'dsl-root')
  });
  const outputPath = options.output ? path.resolve(options.output) : null;
  if (outputPath) {
    await fs.mkdir(path.dirname(outputPath), { recursive: true });
    await fs.writeFile(outputPath, `${JSON.stringify(result, null, 2)}\n`);
  }
  process.stdout.write(`${JSON.stringify(result, null, 2)}\n`);
  if (result.status !== 'pass') process.exitCode = 1;
}

if (import.meta.url === `file://${process.argv[1]}`) {
  await main();
}
