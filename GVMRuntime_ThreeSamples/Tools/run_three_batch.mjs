#!/usr/bin/env node

import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

import {
  loadJson,
  runThreeMatrix,
  writeJson
} from '../../tests/runners/three/node/runner.mjs';
import { lintSampleGpuBoundary } from './lint_sample_gpu_boundary.mjs';
import { lintGeneratedArtifacts } from './lint_generated_artifacts.mjs';
import { lintSingleSampleSources } from './lint_single_sample_policy.mjs';
import { validateThreeBatch } from './validate_three_batch.mjs';
import {
  loadThreeImplementationAliases,
  resolveThreeCaseRandomSeed,
  selectThreeHostExecutable,
  stageThreeScenarioInputs
} from './run_three_candidate_matrix.mjs';

const repositoryRoot = path.resolve(new URL('../..', import.meta.url).pathname);
const defaultManifest = path.join(
  repositoryRoot,
  'GVMRuntime_ThreeSamples',
  'Manifest',
  'three-r185-manifest.json'
);
const requiredPipelines = Object.freeze(['legacy', 'experimental']);
const requiredBackends = Object.freeze(['metal', 'vulkan']);

/** Parses explicit value-bearing CLI options without environment fallbacks. */
export function parseBatchArguments(argv) {
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

/** Resolves a required path explicitly against the repository root. */
function requiredPath(options, name, fallback = null) {
  const value = options[name] ?? fallback;
  if (!value) throw new Error(`Missing --${name}.`);
  return path.resolve(repositoryRoot, value);
}

/** Loads a baseline ledger referenced by either supported batch path spelling. */
async function hydrateBatchBaseline(batch) {
  if (Array.isArray(batch?.baselineStrictCaseIds)) return batch;
  const references = [batch?.baselineStrictCaseIdsSource, batch?.baselineStrictCaseIdsPath]
    .filter((value) => typeof value === 'string' && value.length > 0);
  for (const reference of references) {
    const candidates = [
      path.resolve(repositoryRoot, reference),
      path.resolve(repositoryRoot, 'GVMRuntime_ThreeSamples', reference)
    ];
    for (const candidate of candidates) {
      try {
        const ledger = await loadJson(candidate);
        if (Array.isArray(ledger.strictCaseIds)) {
          return { ...batch, baselineStrictCaseIds: ledger.strictCaseIds };
        }
      } catch {
        // Try the next deterministic path spelling.
      }
    }
  }
  return batch;
}

/** Returns the unique active case records and rejects unsafe batch selections. */
export function validateBatchSelection(manifest, batch) {
  if (!Array.isArray(manifest?.examples) || manifest.examples.length !== 588) {
    throw new Error('The source Three r185 Manifest must contain exactly 588 examples.');
  }
  const activeCases = Array.isArray(batch?.activeCases)
    ? batch.activeCases
    : Array.isArray(batch?.cases) ? batch.cases : [];
  const minimum = Number.isInteger(batch?.minimumNetNewCaseCount)
    ? batch.minimumNetNewCaseCount
    : Number.isInteger(batch?.minimumCaseCount) ? batch.minimumCaseCount : 20;
  if (minimum < 20 || activeCases.length < minimum) {
    throw new Error(`Atomic Three batch requires at least 20 active cases; received ${activeCases.length}.`);
  }
  const ids = activeCases.map((entry) => entry?.caseId);
  if (ids.some((caseId) => typeof caseId !== 'string' || caseId.length === 0)) {
    throw new Error('Every active batch entry must declare a non-empty caseId.');
  }
  for (const entry of activeCases) {
    if (typeof entry.dslEntry !== 'string' || entry.dslEntry.length === 0
        || typeof entry.hostTarget !== 'string' || entry.hostTarget.length === 0) {
      throw new Error(`${entry.caseId ?? '<unknown>'}: active batch entries require dedicated dslEntry and hostTarget.`);
    }
  }
  if (new Set(ids).size !== ids.length) throw new Error('Active batch contains duplicate case IDs.');
  const baselineIds = new Set(batch?.baselineStrictCaseIds ?? []);
  const overlap = ids.filter((caseId) => baselineIds.has(caseId));
  if (overlap.length > 0) throw new Error(`Active batch overlaps the strict baseline: ${overlap.join(', ')}.`);
  const manifestById = new Map(manifest.examples.map((example) => [example.id, example]));
  const selectedExamples = ids.map((caseId) => {
    const example = manifestById.get(caseId);
    if (!example) throw new Error(`Active batch case '${caseId}' is missing from the Manifest.`);
    if (example.status !== 'phase1_required') {
      throw new Error(`${caseId}: active batch cases must be phase1_required, got ${example.status}.`);
    }
    return example;
  });
  if (batch?.selectionPolicy?.msaaEnabled !== false
      || batch?.selectionPolicy?.simulateMsaa !== false) {
    throw new Error('Batch selection must explicitly freeze msaaEnabled=false and simulateMsaa=false.');
  }
  if (batch?.upstreamMsaaPolicy?.simulateMsaa !== false) {
    throw new Error('Batch upstreamMsaaPolicy must explicitly disable MSAA simulation.');
  }
  return { activeCases, selectedExamples, baselineIds };
}

/** Creates a private one-case Manifest view without mutating the locked source. */
export function createSingleCaseManifest(sourceManifest, selectedExample) {
  return {
    ...sourceManifest,
    examples: sourceManifest.examples.map((example) => (
      example.id === selectedExample.id
        ? selectedExample
        : { ...example, status: 'excluded_upstream' }
    ))
  };
}

/** Converts one runner result into the compact case record used by atomic validation. */
export function summarizeCaseRun(example, report, reportPath) {
  const quadrants = report.quadrants.filter((entry) => entry.caseId === example.id);
  const crossComparisons = report.crossComparisons.filter((entry) => entry.caseId === example.id);
  const stabilityComparisons = report.stabilityComparisons.filter((entry) => entry.caseId === example.id);
  const failures = [
    ...quadrants.flatMap((entry) => entry.failures ?? []),
    ...crossComparisons.flatMap((entry) => entry.failures ?? []),
    ...stabilityComparisons.flatMap((entry) => entry.failures ?? [])
  ];
  const status = report.status === 'pass' && failures.length === 0 ? 'pass' : 'fail';
  return {
    caseId: example.id,
    status,
    reportPath,
    scenarioCount: example.scenarios.length,
    quadrantCount: quadrants.length,
    crossComparisonCount: crossComparisons.length,
    stabilityComparisonCount: stabilityComparisons.length,
    gpuBoundaryLintSource: 'atomic-batch-current-source',
    failures
  };
}

/** Runs every active case independently and publishes one aggregate batch ledger. */
export async function runThreeBatch({
  manifest,
  batch,
  buildDir,
  assetRoot,
  oracleRoot,
  runRoot,
  pipelines = [...requiredPipelines],
  backends = [...requiredBackends],
  repetitions = 3,
  timeoutMs = 45_000,
  onProgress = null
}) {
  const effectiveBatch = await hydrateBatchBaseline(batch);
  const { activeCases, selectedExamples: unseededExamples, baselineIds } = validateBatchSelection(manifest, effectiveBatch);
  // The locked Oracle is authoritative for case-specific random streams.
  // Hydrate the selected examples before creating each private Manifest view
  // so runThreeMatrix passes the same seed to the Host and metadata validator.
  const selectedExamples = await Promise.all(unseededExamples.map(async (example) => {
    if (Number.isInteger(example.randomSeed)) return example;
    const lockedSeed = await resolveThreeCaseRandomSeed(example, oracleRoot);
    return lockedSeed === null ? example : { ...example, randomSeed: lockedSeed };
  }));
  if (!requiredPipelines.every((pipeline) => pipelines.includes(pipeline))
      || !requiredBackends.every((backend) => backends.includes(backend))) {
    throw new Error('Atomic batch execution requires Legacy/Experimental × Metal/Vulkan.');
  }
  await fs.rm(runRoot, { recursive: true, force: true });
  await fs.mkdir(runRoot, { recursive: true });
  await stageThreeScenarioInputs(selectedExamples, assetRoot);
  const allQuadrants = [];
  const allCrossComparisons = [];
  const allStabilityComparisons = [];
  const caseResults = [];
  for (const example of selectedExamples) {
    const aliases = await loadThreeImplementationAliases(example.id);
    const hosts = {};
    for (const pipeline of pipelines) {
      hosts[pipeline] = await selectThreeHostExecutable(
        buildDir,
        example,
        pipeline,
        aliases
      );
    }
    const caseRunRoot = path.join(runRoot, 'cases', example.id);
    const report = await runThreeMatrix({
      sourceDir: repositoryRoot,
      runDir: caseRunRoot,
      profile: { hosts, buildDir },
      manifest: createSingleCaseManifest(manifest, example),
      assetRoot,
      oracleRoot,
      status: 'phase1_required',
      group: 'full',
      pipelines,
      backends,
      repetitions,
      timeoutMs,
      onProgress
    });
    const reportPath = path.relative(repositoryRoot, path.join(caseRunRoot, 'summary.json'));
    const caseResult = summarizeCaseRun(example, report, reportPath);
    await writeJson(path.join(caseRunRoot, 'summary.json'), {
      ...report,
      schemaVersion: 1,
      evidenceType: 'strict-example-case',
      caseId: example.id,
      samplePolicy: { mode: 'single-sample', msaaEnabled: false, simulateMsaa: false },
      generatedAt: new Date().toISOString()
    });
    caseResults.push(caseResult);
    allQuadrants.push(...report.quadrants);
    allCrossComparisons.push(...report.crossComparisons);
    allStabilityComparisons.push(...report.stabilityComparisons);
  }
  const sourceViolations = await lintSingleSampleSources(
    path.join(repositoryRoot, 'GVMRuntime_ThreeSamples', 'Dsl')
  );
  const sourceLint = {
    status: sourceViolations.length === 0 ? 'pass' : 'fail',
    violations: sourceViolations
  };
  const gpuBoundaryLint = await lintSampleGpuBoundary(repositoryRoot);
  let generatedArtifactResult;
  try {
    generatedArtifactResult = lintGeneratedArtifacts({
      legacyRoot: path.join(buildDir, 'generated', 'three-r185', 'legacy'),
      experimentalRoot: path.join(buildDir, 'generated', 'three-r185', 'experimental'),
      manifestPath: defaultManifest,
      caseIds: selectedExamples.map((example) => example.id),
      fixtureSelectors: [],
      instancingPasses: []
    });
  } catch (error) {
    generatedArtifactResult = {
      errors: [{
        code: 'generated_artifact_lint_exception',
        context: 'batch',
        message: error instanceof Error ? error.message : String(error)
      }],
      checkedPasses: [],
      legacyRoot: path.join(buildDir, 'generated', 'three-r185', 'legacy'),
      experimentalRoot: path.join(buildDir, 'generated', 'three-r185', 'experimental')
    };
  }
  const generatedArtifactLint = {
    status: generatedArtifactResult.errors.length === 0 ? 'pass' : 'fail',
    checkedPasses: generatedArtifactResult.checkedPasses,
    errors: generatedArtifactResult.errors,
    legacyRoot: generatedArtifactResult.legacyRoot,
    experimentalRoot: generatedArtifactResult.experimentalRoot
  };
  const totalFailures = [
    ...caseResults.flatMap((result) => result.failures.map((failure) => `${result.caseId}: ${failure}`))
  ];
  if (sourceLint.status !== 'pass') {
    totalFailures.push(`single-sample source lint found ${sourceLint.violations.length} violation(s).`);
  }
  if (gpuBoundaryLint.status !== 'pass') {
    totalFailures.push(`GPU boundary lint found ${gpuBoundaryLint.violationCount} violation(s).`);
  }
  if (generatedArtifactLint.status !== 'pass') {
    totalFailures.push(`generated-artifact lint found ${generatedArtifactLint.errors.length} error(s).`);
  }
  return {
    schemaVersion: 1,
    evidenceType: 'strict-example-batch',
    gate: effectiveBatch.batchId ?? 'three-r185-strict-atomic-batch',
    status: caseResults.length === activeCases.length
      && caseResults.every((result) => result.status === 'pass')
      && sourceLint.status === 'pass'
      && gpuBoundaryLint.status === 'pass'
      && generatedArtifactLint.status === 'pass'
      && totalFailures.length === 0 ? 'pass' : 'fail',
    minimumCaseCount: Math.max(20, effectiveBatch.minimumCaseCount ?? 20),
    caseCount: caseResults.length,
    baselineCaseCount: baselineIds.size,
    netNewCaseCount: activeCases.length,
    repeatCount: repetitions,
    pipelines,
    backends,
    samplePolicy: { mode: 'single-sample', msaaEnabled: false, simulateMsaa: false },
    thresholds: {
      normalizedDistancePixelRatio: 0.001,
      meanAbsoluteRgb: 2,
      p99AbsoluteRgb: 16,
      luminanceSsim: 0.995
    },
    totals: {
      scenarios: selectedExamples.reduce((sum, example) => sum + example.scenarios.length, 0),
      quadrants: allQuadrants.length,
      crossComparisons: allCrossComparisons.length,
      stabilityComparisons: allStabilityComparisons.length
    },
    // Preserve the immutable input ledger in the aggregate report so the
    // published batch can be reproduced without consulting the batch
    // definition separately.
    assetLocks: effectiveBatch.assetLocks ?? [],
    inputLocks: effectiveBatch.inputLocks ?? [],
    oracleLocks: effectiveBatch.oracleLocks ?? [],
    assetLockCount: (effectiveBatch.assetLocks ?? []).length,
    inputLockCount: (effectiveBatch.inputLocks ?? []).length,
    oracleLockCount: (effectiveBatch.oracleLocks ?? []).length,
    singleSampleSourceLint: sourceLint,
    gpuBoundaryLint,
    generatedArtifactLint,
    totalFailures,
    caseResults,
    quadrants: allQuadrants,
    crossComparisons: allCrossComparisons,
    stabilityComparisons: allStabilityComparisons
  };
}

/** Executes the batch runner CLI and writes no progress publication on failure. */
async function main() {
  const options = parseBatchArguments(process.argv);
  const manifestPath = requiredPath(options, 'manifest', path.relative(repositoryRoot, defaultManifest));
  const batchPath = requiredPath(options, 'batch');
  const buildDir = requiredPath(options, 'build-dir');
  const assetRoot = requiredPath(options, 'asset-root');
  const oracleRoot = requiredPath(options, 'oracle-root');
  const runRoot = requiredPath(options, 'run-root');
  const [manifest, batch] = await Promise.all([loadJson(manifestPath), loadJson(batchPath)]);
  const effectiveBatch = await hydrateBatchBaseline(batch);
  const selection = validateBatchSelection(manifest, effectiveBatch);
  const pipelines = options.pipeline == null || options.pipeline === 'all'
    ? [...requiredPipelines] : [options.pipeline];
  const backends = options.backend == null || options.backend === 'all'
    ? [...requiredBackends] : [options.backend];
  if (!requiredPipelines.every((pipeline) => pipelines.includes(pipeline))
      || !requiredBackends.every((backend) => backends.includes(backend))) {
    throw new Error('Atomic batch execution requires Legacy/Experimental × Metal/Vulkan.');
  }
  if (options['dry-run'] === 'true') {
    process.stdout.write(`${JSON.stringify({
      status: 'dry-run',
      caseCount: selection.activeCases.length,
      caseIds: selection.activeCases.map((entry) => entry.caseId),
      pipelines,
      backends,
      singleSample: true,
      msaaEnabled: false,
      simulateMsaa: false
    }, null, 2)}\n`);
    return;
  }
  const report = await runThreeBatch({
    manifest,
    batch: effectiveBatch,
    buildDir,
    assetRoot,
    oracleRoot,
    runRoot,
    pipelines,
    backends,
    repetitions: Number(options.repeat ?? 3),
    timeoutMs: Number(options['timeout-ms'] ?? 45_000),
    onProgress: (result, completed) => {
      const prefix = result.status === 'pass' ? 'PASS' : 'FAIL';
      console.log(`[${prefix}] ${completed} ${result.caseId}/${result.scenarioId} ${result.pipeline}/${result.backend} repeat=${result.repetition}`);
      for (const failure of result.failures) console.log(`  ${failure}`);
    }
  });
  const summaryPath = path.join(runRoot, 'summary.json');
  await writeJson(summaryPath, { ...report, generatedAt: new Date().toISOString() });
  const validation = await validateThreeBatch({
    manifestPath,
    batchPath,
    summaryPath,
    dslRoot: path.join(repositoryRoot, 'GVMRuntime_ThreeSamples', 'Dsl')
  });
  if (validation.status !== 'pass') {
    report.status = 'fail';
    report.totalFailures = [
      ...report.totalFailures,
      ...validation.failures.map((failure) => `batch validator: ${failure}`)
    ];
    await writeJson(summaryPath, { ...report, validation, generatedAt: new Date().toISOString() });
  }
  process.stdout.write(`${JSON.stringify({
    status: report.status,
    caseCount: report.caseCount,
    netNewCaseCount: report.netNewCaseCount,
    totals: report.totals,
    summaryPath: path.relative(repositoryRoot, summaryPath),
    failures: report.totalFailures,
    validationStatus: validation.status
  }, null, 2)}\n`);
  if (report.status !== 'pass') process.exitCode = 1;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
