#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';

const REQUIRED_QUADRANTS = Object.freeze([
  'legacy-metal',
  'legacy-vulkan',
  'experimental-metal',
  'experimental-vulkan'
]);

/** Keeps the repository-wide image gate in one place for atomic closure. */
const STRICT_IMAGE_THRESHOLDS = Object.freeze({
  normalizedDistancePixelRatio: 0.001,
  meanAbsoluteRgb: 2,
  p99AbsoluteRgb: 16,
  luminanceSsim: 0.995
});

/** Parses explicit command-line options without environment-variable fallbacks. */
export function parseBatchClosureArguments(argv) {
  const options = {};
  for (let index = 2; index < argv.length; index += 2) {
    const name = argv[index];
    const value = argv[index + 1];
    if (!name?.startsWith('--') || value === undefined) {
      throw new Error(`Invalid argument near '${name ?? '<end>'}'.`);
    }
    const key = name.slice(2);
    if (Object.hasOwn(options, key)) throw new Error(`Duplicate --${key}.`);
    options[key] = value;
  }
  for (const required of ['batch', 'summary-root', 'output']) {
    if (!options[required]) throw new Error(`Missing --${required}.`);
  }
  return options;
}

/** Returns the normalized execution quadrant label used by the strict batch gate. */
function executionQuadrant(execution) {
  if (typeof execution.quadrant === 'string') return execution.quadrant;
  if (typeof execution.pipeline === 'string' && typeof execution.backend === 'string') {
    return `${execution.pipeline}-${execution.backend}`;
  }
  return '';
}

/** Returns the formal runner executions without weakening legacy summary support. */
function strictExecutions(summary, caseId) {
  if (Array.isArray(summary.executions)) {
    return summary.executions.filter(
      (execution) => execution.caseId == null || execution.caseId === caseId);
  }
  if (Array.isArray(summary.quadrants)) {
    return summary.quadrants
      .filter((execution) => execution.caseId == null || execution.caseId === caseId)
      .map((execution) => ({
        ...execution,
        passed: execution.status === 'pass'
          && execution.validation?.status === 'pass'
      }));
  }
  return [];
}

/** Checks every formal-runner aggregate comparison and lint collection. */
function validateFormalRunnerEvidence(summary, failures) {
  if (summary.status !== 'pass') failures.push('formal summary status is not pass');
  if (!Number.isInteger(summary.runCount)
      || summary.runCount !== summary.expectedRunCount
      || summary.runCount !== summary.quadrants?.length) {
    failures.push('formal summary run count is incomplete');
  }
  if (!Number.isInteger(summary.repeatCount) || summary.repeatCount < 3) {
    failures.push('formal summary does not prove three repetitions');
  }
  for (const [name, entries] of [
    ['cross-quadrant', summary.crossComparisons],
    ['stability', summary.stabilityComparisons]
  ]) {
    if (!Array.isArray(entries) || entries.length === 0
        || entries.some((entry) => entry.status !== 'pass')) {
      failures.push(`${name} comparisons are not all passed`);
    }
  }
  const generatedArtifactReports = Array.isArray(summary.generatedArtifacts)
    ? summary.generatedArtifacts
    : [summary.generatedArtifacts];
  if (generatedArtifactReports.length === 0
      || generatedArtifactReports.some((report) => report?.status !== 'pass')) {
    failures.push('generated artifact lint is not passed');
  }
  if (summary.gpuBoundaryLint?.status !== 'pass') {
    failures.push('GPU boundary lint is not passed');
  }
  for (const execution of summary.quadrants ?? []) {
    const oraclePaths = execution.validation?.oraclePaths;
    const metrics = execution.validation?.metrics;
    if (execution.status !== 'pass'
        || execution.validation?.status !== 'pass'
        || typeof oraclePaths?.rgbaPath !== 'string'
        || oraclePaths.rgbaPath.length === 0
        || !metrics
        || !Number.isFinite(metrics.normalizedDistancePixelRatio)
        || metrics.normalizedDistancePixelRatio >= STRICT_IMAGE_THRESHOLDS.normalizedDistancePixelRatio
        || !Number.isFinite(metrics.meanAbsoluteRgb)
        || metrics.meanAbsoluteRgb > STRICT_IMAGE_THRESHOLDS.meanAbsoluteRgb
        || !Number.isFinite(metrics.p99AbsoluteRgb)
        || metrics.p99AbsoluteRgb > STRICT_IMAGE_THRESHOLDS.p99AbsoluteRgb
        || !Number.isFinite(metrics.luminanceSsim)
        || metrics.luminanceSsim < STRICT_IMAGE_THRESHOLDS.luminanceSsim) {
      failures.push('one or more formal executions lack passed Oracle evidence');
      break;
    }
  }
}

/** Checks one standard summary without accepting implementation-smoke evidence. */
export function validateStrictCaseSummary(summary, caseId) {
  const failures = [];
  if (summary.caseId !== caseId
      && !summary.caseIds?.includes(caseId)
      && !summary.selectedCases?.includes(caseId)) {
    failures.push('summary does not identify the requested case');
  }
  if (summary.gateKind === 'implementation-smoke' || summary.strictGateEvidence === false) {
    failures.push('implementation smoke is not strict gate evidence');
  }
  const formalRunnerSummary = Array.isArray(summary.quadrants);
  if (formalRunnerSummary) {
    validateFormalRunnerEvidence(summary, failures);
  } else {
    if (summary.passed !== true) failures.push('summary is not globally passed');
    if (summary.oracleComparisonPassed === false || summary.oracleComparison === false) {
      failures.push('oracle comparison is not passed');
    }
    if (summary.threeRunStabilityPassed === false || summary.stabilityPassed === false) {
      failures.push('three-run stability is not passed');
    }
    if (summary.crossQuadrantComparisonPassed === false || summary.crossQuadrantPassed === false) {
      failures.push('cross-quadrant comparison is not passed');
    }
    if (summary.lintPassed === false || summary.generatedArtifactLintPassed === false
        || summary.renderSetPolicyLintPassed === false || summary.gpuBoundaryLintPassed === false) {
      failures.push('one or more required lints are not passed');
    }
  }
  const executions = strictExecutions(summary, caseId);
  const quadrants = new Set(executions.filter((execution) => execution.passed === true).map(executionQuadrant));
  for (const quadrant of REQUIRED_QUADRANTS) {
    if (!quadrants.has(quadrant)) failures.push(`missing passed quadrant '${quadrant}'`);
  }
  const repetitions = new Map(REQUIRED_QUADRANTS.map((quadrant) => [quadrant, new Set()]));
  for (const execution of executions) {
    const quadrant = executionQuadrant(execution);
    if (!repetitions.has(quadrant) || execution.passed !== true) continue;
    const repetition = execution.repetition ?? execution.runIndex ?? execution.repeatIndex;
    if (Number.isInteger(repetition)) repetitions.get(quadrant).add(repetition);
  }
  const carriesExplicitRepetitions = executions.some((execution) =>
    Number.isInteger(execution.repetition ?? execution.runIndex ?? execution.repeatIndex));
  if (carriesExplicitRepetitions) {
    for (const [quadrant, values] of repetitions) {
      if (values.size < 3) failures.push(`${quadrant} has ${values.size} stable repetitions; expected at least 3`);
    }
  } else if (!formalRunnerSummary
      && summary.repetitionCount !== 3
      && summary.stableRunCount !== 3) {
    failures.push('summary does not prove three stable repetitions');
  }
  return failures;
}

/** Resolves a case summary from either a direct or nested standard report directory. */
function findCaseSummary(summaryRoot, caseId) {
  const candidates = [
    path.join(summaryRoot, caseId, 'summary.json'),
    path.join(summaryRoot, `${caseId}.summary.json`)
  ];
  return candidates.find((candidate) => fs.existsSync(candidate)) ?? null;
}

/** Validates that one batch has twenty distinct strict passes and no blocked-case credit. */
export function validateBatchClosure(batch, summaryRoot) {
  if (batch?.schemaVersion !== 1 && batch?.schemaVersion !== 2) {
    throw new Error('Batch schemaVersion must be 1 or 2.');
  }
  const minimumStrictPassCount = batch.minimumNetNewCaseCount
    ?? batch.minimumStrictPassCount;
  if (!Number.isInteger(minimumStrictPassCount) || minimumStrictPassCount < 20) {
    throw new Error('minimumNetNewCaseCount/minimumStrictPassCount must be at least 20.');
  }
  const activeCases = Array.isArray(batch.activeCases)
    ? batch.activeCases.map((entry) => typeof entry === 'string' ? entry : entry?.caseId)
      .filter((caseId) => typeof caseId === 'string' && caseId.length > 0)
    : [];
  const primaryCases = activeCases.length > 0
    ? activeCases
    : (Array.isArray(batch.primaryCases) ? batch.primaryCases : []);
  const fallbackCases = Array.isArray(batch.fallbackCases) ? batch.fallbackCases : [];
  const blockedIds = new Set((batch.blockedCases ?? []).map((entry) => entry.caseId));
  const orderedCandidates = [...primaryCases, ...fallbackCases];
  if (new Set(orderedCandidates).size !== orderedCandidates.length) {
    throw new Error('Primary and fallback case IDs must be globally unique.');
  }
  const baselineIds = new Set(
    Array.isArray(batch.baselineStrictCaseIds) ? batch.baselineStrictCaseIds : []);
  const baselineConflicts = orderedCandidates.filter((caseId) => baselineIds.has(caseId));
  const selectionFailures = [];
  if (baselineConflicts.length > 0) {
    selectionFailures.push(
      `candidate IDs already present in the strict baseline: ${baselineConflicts.join(', ')}`);
  }
  if (activeCases.length > 0 && activeCases.length !== minimumStrictPassCount) {
    selectionFailures.push(
      `activeCases contains ${activeCases.length} IDs; expected ${minimumStrictPassCount}`);
  }
  const results = [];
  for (const caseId of orderedCandidates) {
    if (blockedIds.has(caseId)) {
      results.push({ caseId, state: 'blocked', failures: ['case is explicitly blocked'] });
      continue;
    }
    const summaryPath = findCaseSummary(summaryRoot, caseId);
    if (!summaryPath) {
      results.push({ caseId, state: 'missing', failures: ['strict summary is missing'] });
      continue;
    }
    const summary = JSON.parse(fs.readFileSync(summaryPath, 'utf8'));
    const failures = validateStrictCaseSummary(summary, caseId);
    results.push({
      caseId,
      state: failures.length === 0 ? 'strict-pass' : 'failed',
      summaryPath,
      failures
    });
  }
  const strictCases = results.filter((result) => result.state === 'strict-pass'
      && !baselineIds.has(result.caseId))
    .slice(0, minimumStrictPassCount);
  return {
    schemaVersion: batch.schemaVersion,
    batchId: batch.batchId,
    minimumStrictPassCount,
    minimumNetNewCaseCount: minimumStrictPassCount,
    activeCaseCount: activeCases.length > 0 ? activeCases.length : primaryCases.length,
    baselineStrictCaseCount: baselineIds.size,
    baselineConflicts,
    selectionFailures,
    strictPassCount: strictCases.length,
    passed: selectionFailures.length === 0
      && strictCases.length >= minimumStrictPassCount,
    strictCases: strictCases.map((result) => result.caseId),
    results
  };
}

/** Runs the batch validator and writes an auditable aggregate result. */
export function executeBatchClosure(argv = process.argv) {
  const options = parseBatchClosureArguments(argv);
  const batchPath = path.resolve(options.batch);
  const summaryRoot = path.resolve(options['summary-root']);
  const outputPath = path.resolve(options.output);
  const batch = JSON.parse(fs.readFileSync(batchPath, 'utf8'));
  const result = validateBatchClosure(batch, summaryRoot);
  fs.mkdirSync(path.dirname(outputPath), { recursive: true });
  fs.writeFileSync(outputPath, `${JSON.stringify(result, null, 2)}\n`);
  if (!result.passed) {
    throw new Error(
      `Batch '${result.batchId}' has ${result.strictPassCount}/${result.minimumStrictPassCount} strict passes.`
    );
  }
  process.stdout.write(
    `Phase 1 closure batch passed: ${result.strictPassCount}/${result.minimumStrictPassCount} examples.\n`
  );
  return result;
}

if (import.meta.url === `file://${process.argv[1]}`) {
  try {
    executeBatchClosure();
  } catch (error) {
    process.stderr.write(`${error.stack ?? error.message}\n`);
    process.exitCode = 1;
  }
}
