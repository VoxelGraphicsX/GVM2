#!/usr/bin/env node

import crypto from 'node:crypto';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { lintSampleGpuBoundary } from './lint_sample_gpu_boundary.mjs';
import {
  compareThreeCaptures,
  loadRgbaArtifact
} from '../../tests/runners/three/node/image-comparison.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../..');
const requiredPipelines = Object.freeze(['legacy', 'experimental']);
const requiredBackends = Object.freeze(['metal', 'vulkan']);
const expectedThresholds = Object.freeze({
  normalizedDistancePixelRatio: 0.001,
  meanAbsoluteRgb: 2,
  p99AbsoluteRgb: 16,
  luminanceSsim: 0.995
});
const forbiddenSingleSampleSourcePatterns = Object.freeze([
  ['supersample', /\bsupersampl(?:e|ed|ing)\w*/giu],
  ['MSAA emulation', /\b(?:emulat|simulat)\w*\s+(?:the\s+)?MSAA\b/giu],
  ['antialias resolve', /\bantialias(?:ing)?[\s_-]+resolve\b/giu],
  ['coverage sample', /\bcoverage[\s_-]+samples?\b/giu],
  ['rotated sample grid', /\brotated[\s_-]+(?:sample|raster)[\s_-]+(?:grid|positions?)\b/giu]
]);

/** Returns the stable digest used to lock the strict-case baseline of a net-new batch. */
export function digestCaseIds(caseIds) {
  return crypto.createHash('sha256')
    .update(`${[...caseIds].sort().join('\n')}\n`)
    .digest('hex');
}

/**
 * Determines whether an explicit single-case diagnostic report is a complete
 * strict matrix and may be normalized into the atomic batch contract.
 */
export function isCompleteSingleCaseDiagnosticReport(report, example) {
  const selectedCases = Array.isArray(report?.selectedCases)
    ? report.selectedCases : [];
  const coverage = report?.coverage;
  const quadrants = Array.isArray(report?.quadrants) ? report.quadrants : [];
  const crossComparisons = Array.isArray(report?.crossComparisons)
    ? report.crossComparisons : [];
  const stabilityComparisons = Array.isArray(report?.stabilityComparisons)
    ? report.stabilityComparisons : [];
  const scenarioCount = (example?.scenarios ?? []).length;
  const allPass = (entries) => entries.length > 0
    && entries.every((entry) => entry?.status === 'pass');
  const singleSample = report?.samplePolicy == null
    || (report.samplePolicy.mode === 'single-sample'
      && report.samplePolicy.msaaEnabled === false
      && report.samplePolicy.simulateMsaa === false);
  return report?.status === 'diagnostic-pass'
    && selectedCases.length === 1
    && selectedCases[0] === example?.id
    && coverage?.caseId === example?.id
    && coverage?.scenarioCount === scenarioCount
    && coverage?.quadrantCount === quadrants.length
    && coverage?.crossComparisonCount === crossComparisons.length
    && coverage?.stabilityComparisonCount === stabilityComparisons.length
    && quadrants.length === scenarioCount * 12
    && crossComparisons.length === scenarioCount * 12
    && stabilityComparisons.length === scenarioCount * 8
    && allPass(quadrants)
    && allPass(crossComparisons)
    && allPass(stabilityComparisons)
    && coverage.oracleFailures === 0
    && coverage.crossFailures === 0
    && coverage.stabilityFailures === 0
    && singleSample
    && (!Array.isArray(report.generatedArtifacts)
      || report.generatedArtifacts.every((entry) => entry?.status === 'pass'))
    && (report.gpuBoundaryLint == null || report.gpuBoundaryLint.status === 'pass');
}

/** Validates that an atomic batch contains only cases outside its locked strict baseline. */
export function validateNetNewBaseline(batch, caseIds) {
  if (batch.schemaVersion < 2) {
    return {
      baselineCaseCount: 0,
      netNewCaseCount: caseIds.length
    };
  }
  if (!Array.isArray(batch.baselineStrictCaseIds)
      || new Set(batch.baselineStrictCaseIds).size !== batch.baselineStrictCaseIds.length) {
    throw new Error('Schema-2 strict batch requires unique baselineStrictCaseIds.');
  }
  const actualDigest = digestCaseIds(batch.baselineStrictCaseIds);
  if (actualDigest !== batch.baselineStrictCaseIdsSha256) {
    throw new Error(`Strict baseline digest ${actualDigest} differs from the locked ${batch.baselineStrictCaseIdsSha256}.`);
  }
  const baseline = new Set(batch.baselineStrictCaseIds);
  const overlapping = caseIds.filter((caseId) => baseline.has(caseId));
  if (overlapping.length > 0) {
    throw new Error(`Strict batch repeats baseline cases: ${overlapping.join(', ')}.`);
  }
  const minimumNetNewCaseCount = batch.minimumNetNewCaseCount ?? batch.minimumCaseCount;
  if (caseIds.length < minimumNetNewCaseCount || minimumNetNewCaseCount < 20) {
    throw new Error(`Strict batch contains ${caseIds.length} net-new cases; at least ${minimumNetNewCaseCount} are required.`);
  }
  return {
    baselineCaseCount: baseline.size,
    netNewCaseCount: caseIds.length
  };
}

/** Validates immutable asset and Oracle lock identities declared by one atomic batch. */
export function validateBatchLocks(batch) {
  const validateLocks = (locks, identityFields, label) => {
    if (locks == null) return 0;
    if (!Array.isArray(locks)) throw new Error(`${label} must be an array.`);
    const identities = new Set();
    for (const lock of locks) {
      const identity = identityFields.map((field) => lock?.[field]).join('|');
      if (identityFields.some((field) => typeof lock?.[field] !== 'string' || lock[field] === '')) {
        throw new Error(`${label} contains an incomplete identity.`);
      }
      if (!/^[0-9a-f]{64}$/.test(lock.sha256 ?? '')) {
        throw new Error(`${label} ${identity} has an invalid SHA-256 digest.`);
      }
      if (identities.has(identity)) throw new Error(`${label} repeats ${identity}.`);
      identities.add(identity);
    }
    return locks.length;
  };
  return {
    assetLockCount: validateLocks(batch.assetLocks, ['path'], 'assetLocks'),
    inputLockCount: validateLocks(batch.inputLocks, ['path'], 'inputLocks'),
    oracleLockCount: validateLocks(batch.oracleLocks, ['caseId', 'scenarioId'], 'oracleLocks')
  };
}

/** Returns source-level violations of the batch's mandatory single-sample rendering policy. */
export function findSingleSampleSourceViolations(source, sourcePath = '<source>') {
  const violations = [];
  for (const [reason, pattern] of forbiddenSingleSampleSourcePatterns) {
    pattern.lastIndex = 0;
    for (const match of source.matchAll(pattern)) {
      const line = source.slice(0, match.index).split('\n').length;
      violations.push(`${sourcePath}:${line}: ${reason}: ${match[0]}`);
    }
  }
  return violations;
}

/** Finds every DSL directory that declares at least one active atomic-batch case. */
async function findActiveDslDirectories(caseIds) {
  const activeCases = new Set(caseIds);
  const dslRoot = path.join(repositoryRoot, 'GVMRuntime_ThreeSamples', 'Dsl');
  const directories = [];
  for (const entry of await fs.readdir(dslRoot, { withFileTypes: true })) {
    if (!entry.isDirectory()) continue;
    const directoryPath = path.join(dslRoot, entry.name);
    const inventoryPath = path.join(directoryPath, 'implemented-cases.json');
    let inventory;
    try {
      inventory = JSON.parse(await fs.readFile(inventoryPath, 'utf8'));
    } catch (error) {
      if (error.code === 'ENOENT') continue;
      throw new Error(`Could not load ${inventoryPath}: ${error.message}`);
    }
    if ((inventory.cases ?? []).some((item) => activeCases.has(item.caseId))) {
      directories.push(directoryPath);
    }
  }
  return directories;
}

/**
 * Loads the unique implementation inventory record for every active batch case.
 *
 * A strict batch must be traceable to an explicit DSL/Host declaration rather
 * than relying on a historical report path alone.  Duplicate declarations are
 * retained as validation failures so a stale shard cannot silently shadow the
 * active implementation.
 */
async function loadActiveImplementationInventories(caseIds) {
  const activeCases = new Set(caseIds);
  const dslRoot = path.join(repositoryRoot, 'GVMRuntime_ThreeSamples', 'Dsl');
  const inventories = new Map();
  const duplicateCaseIds = new Set();
  for (const entry of await fs.readdir(dslRoot, { withFileTypes: true })) {
    if (!entry.isDirectory()) continue;
    const directoryPath = path.join(dslRoot, entry.name);
    const inventoryPath = path.join(directoryPath, 'implemented-cases.json');
    let inventory;
    try {
      inventory = JSON.parse(await fs.readFile(inventoryPath, 'utf8'));
    } catch (error) {
      if (error.code === 'ENOENT') continue;
      throw new Error(`Could not load ${inventoryPath}: ${error.message}`);
    }
    for (const item of inventory.cases ?? []) {
      if (!activeCases.has(item?.caseId)) continue;
      if (inventories.has(item.caseId)) duplicateCaseIds.add(item.caseId);
      inventories.set(item.caseId, {
        ...item,
        shard: item.shard ?? entry.name,
        inventoryPath
      });
    }
  }
  return { inventories, duplicateCaseIds };
}

/**
 * Validates the implementation declaration that owns one strict batch case.
 *
 * The declaration is intentionally independent from image evidence: it binds
 * the strict report to the named DSL entry, Host target, scene passes and
 * RenderSet type, while allowing an older inventory to use `strictReport`.
 */
export function validateImplementationInventory(batchCase, inventory, duplicate = false) {
  const failures = [];
  if (!inventory) {
    failures.push('缺少唯一 implemented-cases.json 实现声明。');
    return failures;
  }
  if (duplicate) failures.push('存在多个 implemented-cases.json 声明，无法确定唯一实现。');
  if (inventory.implementationLevel !== 'strict-pass') {
    failures.push(`实现等级为 ${inventory.implementationLevel ?? 'missing'}，不是 strict-pass。`);
  }
  for (const field of ['dslEntry', 'hostTarget']) {
    if (typeof inventory[field] !== 'string' || inventory[field].length === 0) {
      failures.push(`实现声明缺少 ${field}。`);
    }
    if (batchCase?.[field] != null && batchCase[field] !== inventory[field]) {
      failures.push(`批次 ${field}=${batchCase[field]} 与实现声明 ${inventory[field]} 不一致。`);
    }
  }
  const scenePasses = inventory.scenePasses;
  const screenPasses = inventory.screenPasses;
  const screenOnlyCase = inventory.renderSetType === null
    && Array.isArray(screenPasses)
    && screenPasses.length > 0
    && screenPasses.every((name) => typeof name === 'string' && name.length > 0);
  // A screen-only example has no scene geometry by definition.  It is valid
  // for that inventory to expose only screenPasses when it explicitly opts
  // out of RenderSet; geometry cases still require a non-empty scenePasses.
  if (!Array.isArray(scenePasses)
      || scenePasses.some((name) => typeof name !== 'string' || name.length === 0)
      || (scenePasses.length === 0 && !screenOnlyCase)) {
    failures.push('实现声明缺少有效 scenePasses（纯 screen 用例必须声明非空 screenPasses）。');
  }
  if (!Object.hasOwn(inventory, 'renderSetType')) {
    failures.push('实现声明缺少 renderSetType 字段。');
  }
  const reportPath = inventory.strictReport ?? inventory.reportPath;
  if (typeof reportPath !== 'string' || reportPath.length === 0) {
    failures.push('实现声明缺少 strictReport 或 reportPath。');
  }
  return failures;
}

/** Verifies that active DSL sources neither request nor emulate multisample antialiasing. */
async function validateSingleSampleSources(batch, caseIds) {
  const policy = batch.upstreamMsaaPolicy;
  if (policy?.mode !== 'single-sample-rendering'
      || policy?.excludeExamples !== false
      || policy?.simulateMsaa !== false
      || policy?.requireMsaaParity !== false) {
    throw new Error('Atomic batch must declare normal single-sample rendering without exclusions or MSAA simulation.');
  }
  const violations = [];
  const directories = await findActiveDslDirectories(caseIds);
  for (const directoryPath of directories) {
    for (const entry of await fs.readdir(directoryPath, { withFileTypes: true })) {
      if (!entry.isFile() || !/\.(?:h|hpp|cpp|mjs)$/u.test(entry.name)) continue;
      const sourcePath = path.join(directoryPath, entry.name);
      const source = await fs.readFile(sourcePath, 'utf8');
      violations.push(...findSingleSampleSourceViolations(
        source,
        path.relative(repositoryRoot, sourcePath)
      ));
    }
  }
  return {
    status: violations.length === 0 ? 'pass' : 'fail',
    scannedDirectoryCount: directories.length,
    violations
  };
}

/** Parses unique value-bearing long-form command-line options. */
function parseArguments(argv) {
  const options = {};
  for (let index = 2; index < argv.length; index += 2) {
    const option = argv[index];
    const value = argv[index + 1];
    if (!option?.startsWith('--') || value == null || value.startsWith('--')) {
      throw new Error(`Expected --option value pair near '${option ?? '<end>'}'.`);
    }
    const name = option.slice(2);
    if (Object.hasOwn(options, name)) throw new Error(`Duplicate --${name}.`);
    options[name] = value;
  }
  return options;
}

/** Resolves one required path relative to the repository root. */
function requireRepositoryPath(options, name) {
  if (!options[name]) throw new Error(`Missing required --${name} path.`);
  return path.resolve(repositoryRoot, options[name]);
}

/** Loads a required JSON document with source-path diagnostics. */
async function loadJson(documentPath) {
  try {
    return JSON.parse(await fs.readFile(documentPath, 'utf8'));
  } catch (error) {
    throw new Error(`Could not load ${documentPath}: ${error.message}`);
  }
}

/** Returns the threshold object used by any supported strict report schema. */
function getThresholds(report) {
  return report.thresholds
    ?? report.comparisonThresholds
    ?? report.coverage?.thresholds
    ?? null;
}

/** Adds one failure when a report does not preserve the global image thresholds. */
function validateThresholds(report, failures) {
  const thresholds = getThresholds(report);
  if (isRunComparisonReport(report) || report.evidenceType === 'strict-case-closure' || Array.isArray(report.comparisons)) {
    for (const comparison of report.comparisons ?? []) {
      const metrics = comparison?.metrics;
      if (!metrics) {
        failures.push('严格报告的 comparison 缺少图像指标。');
        continue;
      }
      if (metrics.normalizedDistancePixelRatio >= expectedThresholds.normalizedDistancePixelRatio
          || metrics.meanAbsoluteRgb > expectedThresholds.meanAbsoluteRgb
          || metrics.p99AbsoluteRgb > expectedThresholds.p99AbsoluteRgb
          || metrics.luminanceSsim < expectedThresholds.luminanceSsim) {
        failures.push('严格报告包含不满足统一图像阈值的 comparison。');
      }
    }
    return;
  }
  if (!thresholds) {
    failures.push('严格报告未记录统一图像阈值。');
    return;
  }
  for (const [name, expected] of Object.entries(expectedThresholds)) {
    if (thresholds[name] !== expected) {
      failures.push(`图像阈值 ${name}=${thresholds[name]}，预期 ${expected}。`);
    }
  }
}

/** Recursively discovers legacy strict-run capture files below one report directory. */
async function discoverLegacyQuadrants(reportPath, caseId, scenarioIds) {
  const reportDirectory = path.dirname(path.resolve(repositoryRoot, reportPath));
  const scenarioSet = new Set(scenarioIds);
  const quadrants = [];
  const pending = [reportDirectory];
  const parentDirectory = path.dirname(reportDirectory);
  if (parentDirectory !== reportDirectory) pending.push(parentDirectory);
  while (pending.length > 0) {
    const directory = pending.pop();
    let entries;
    try {
      entries = await fs.readdir(directory, { withFileTypes: true });
    } catch {
      continue;
    }
    for (const entry of entries) {
      const entryPath = path.join(directory, entry.name);
      if (entry.isDirectory()) {
        pending.push(entryPath);
        continue;
      }
      if (entry.name !== 'capture.rgba' && entry.name !== 'final.rgba') continue;
      const metadataName = entry.name === 'capture.rgba' ? 'capture.json' : 'final.json';
      const metadataPath = path.join(directory, metadataName);
      let metadata;
      try {
        metadata = JSON.parse(await fs.readFile(metadataPath, 'utf8'));
      } catch {
        continue;
      }
      if (metadata.caseId !== caseId
          || !scenarioSet.has(metadata.scenarioId)
          || !requiredPipelines.includes(metadata.pipeline)
          || !requiredBackends.includes(metadata.backend)
          || metadata.sampleCount != null && metadata.sampleCount !== 1
          || metadata.samplePolicy?.msaaEnabled === true
          || metadata.samplePolicy?.simulateMsaa === true
          || metadata.samplePolicy?.msaaEmulated === true) {
        continue;
      }
      const repetitionMatch = /(?:run|rep|repeat)-(\d+)$/u.exec(path.basename(directory));
      if (!repetitionMatch) continue;
      quadrants.push({
        caseId,
        scenarioId: metadata.scenarioId,
        pipeline: metadata.pipeline,
        backend: metadata.backend,
        repetition: Number(repetitionMatch[1]),
        status: 'pass',
        artifacts: {
          rgbaPath: entryPath,
          metadataPath
        }
      });
    }
  }
  const unique = new Map();
  for (const quadrant of quadrants.sort((left, right) => (
    left.artifacts.rgbaPath.localeCompare(right.artifacts.rgbaPath)
  ))) {
    const key = `${quadrant.scenarioId}|${quadrant.pipeline}|${quadrant.backend}|${quadrant.repetition}`;
    if (!unique.has(key)) unique.set(key, quadrant);
  }
  return [...unique.values()];
}

/** Returns whether a report uses the dedicated runner's run/comparison evidence schema. */
export function isRunComparisonReport(report) {
  return Array.isArray(report?.runs)
    && Array.isArray(report?.comparisons)
    && Number.isInteger(report?.executionCount)
    && Number.isInteger(report?.oracleComparisonCount)
    && Number.isInteger(report?.crossQuadrantComparisonCount)
    && Number.isInteger(report?.stabilityComparisonCount);
}

/** Converts supported strict report schemas into the common aggregate verifier shape. */
export function normalizeCaseEvidence(
  report,
  caseId,
  fallbackScenarioId = null,
  discoveredQuadrants = []
) {
  if (!isRunComparisonReport(report)) {
    return {
      quadrants: (report.quadrants ?? []).filter((entry) => entry.caseId === caseId)
        .map((entry) => ({
          ...entry,
          artifacts: {
            ...entry.artifacts,
            // Wave-7 formal reports used capturePath/finalPath before the
            // common artifacts.rgbaPath field was introduced. Preserve the
            // recorded path; do not synthesize missing evidence.
            rgbaPath: entry.artifacts?.rgbaPath ?? entry.capturePath ?? entry.finalPath,
            metadataPath: entry.artifacts?.metadataPath ?? entry.metadataPath
          }
        }))
        .concat(discoveredQuadrants),
      crossComparisons: (report.crossComparisons ?? [])
        .filter((entry) => entry.caseId == null || entry.caseId === caseId)
        .map((entry) => ({
          ...entry,
          kind: entry.kind ?? 'cross-quadrant',
          status: entry.status ?? (entry.failures?.length === 0 ? 'pass' : 'fail')
        }))
        .concat((report.comparisons ?? [])
          .filter((entry) => entry.kind === 'cross-quadrant'
            && (entry.caseId == null || entry.caseId === caseId))
          .map((entry) => ({
            ...entry,
            kind: 'cross-quadrant',
            status: entry.status ?? (entry.failures?.length === 0 ? 'pass' : 'fail')
          }))),
      stabilityComparisons: (report.stabilityComparisons ?? [])
        .filter((entry) => entry.caseId == null || entry.caseId === caseId)
        .map((entry) => ({
          ...entry,
          kind: entry.kind ?? 'stability',
          status: entry.status ?? (entry.failures?.length === 0 ? 'pass' : 'fail')
        }))
        .concat((report.comparisons ?? [])
          .filter((entry) => entry.kind === 'stability'
            && (entry.caseId == null || entry.caseId === caseId))
          .map((entry) => ({
            ...entry,
            kind: 'stability',
            status: entry.status ?? (entry.failures?.length === 0 ? 'pass' : 'fail')
          })))
    };
  }
  const statusOf = (entry) => entry.status ?? (entry.failures?.length === 0 ? 'pass' : 'fail');
  const artifactRoot = report.artifactRoot ?? report.runRoot ?? report.captureRoot ?? null;
  const resolveArtifactPath = (value) => {
    if (typeof value !== 'string' || value.length === 0) return value;
    if (path.isAbsolute(value)) return value;
    if (typeof artifactRoot === 'string' && artifactRoot.length > 0) {
      return path.resolve(repositoryRoot, artifactRoot, value);
    }
    return path.resolve(repositoryRoot, value);
  };
  return {
    quadrants: report.runs.map((entry) => ({
      caseId,
      scenarioId: entry.scenario ?? entry.scenarioId ?? fallbackScenarioId,
      pipeline: entry.pipeline,
      backend: entry.backend,
      repetition: entry.repetition,
      status: 'pass',
      artifacts: {
        rgbaPath: resolveArtifactPath(entry.rgbaPath),
        metadataPath: resolveArtifactPath(entry.metadataPath)
      }
    })),
    crossComparisons: report.comparisons
      .filter((entry) => entry.kind === 'cross-quadrant')
      .map((entry) => ({ ...entry, caseId, status: statusOf(entry) })),
    stabilityComparisons: report.comparisons
      .filter((entry) => entry.kind === 'stability')
      .map((entry) => ({ ...entry, caseId, status: statusOf(entry) }))
  };
}

/** Compares one capture pair and returns a strict cross-quadrant evidence record. */
async function compareQuadrantPair(left, right, record) {
  const [leftCapture, rightCapture] = await Promise.all([
    loadRgbaArtifact(left.artifacts.rgbaPath, left.artifacts.metadataPath),
    loadRgbaArtifact(right.artifacts.rgbaPath, right.artifacts.metadataPath)
  ]);
  const comparison = compareThreeCaptures(leftCapture, rightCapture);
  return {
    ...record,
    status: comparison.failures.length === 0 ? 'pass' : 'fail',
    metrics: comparison.metrics,
    failures: comparison.failures
  };
}

/** Rebuilds the full four-pair cross-quadrant matrix from retained RGBA artifacts. */
async function deriveCrossComparisons(quadrants, scenarioIds) {
  const byKey = new Map(quadrants.map((entry) => [
    `${entry.scenarioId}|${entry.pipeline}|${entry.backend}|${entry.repetition}`,
    entry
  ]));
  const pairs = [
    ['legacy-metal', 'experimental-metal'],
    ['legacy-vulkan', 'experimental-vulkan'],
    ['legacy-metal', 'legacy-vulkan'],
    ['experimental-metal', 'experimental-vulkan']
  ];
  const results = [];
  for (const scenarioId of scenarioIds) {
    for (let repetition = 1; repetition <= 3; repetition += 1) {
      for (const [leftQuadrant, rightQuadrant] of pairs) {
        const [leftPipeline, leftBackend] = leftQuadrant.split('-');
        const [rightPipeline, rightBackend] = rightQuadrant.split('-');
        const left = byKey.get(`${scenarioId}|${leftPipeline}|${leftBackend}|${repetition}`);
        const right = byKey.get(`${scenarioId}|${rightPipeline}|${rightBackend}|${repetition}`);
        if (!left || !right) continue;
        results.push(await compareQuadrantPair(left, right, {
          kind: 'cross-quadrant',
          caseId: left.caseId,
          scenarioId,
          leftQuadrant,
          rightQuadrant,
          repetition
        }));
      }
    }
  }
  return results;
}

/** Rebuilds repeat-2/repeat-3 stability comparisons from retained RGBA artifacts. */
async function deriveStabilityComparisons(quadrants, scenarioIds) {
  const byKey = new Map(quadrants.map((entry) => [
    `${entry.scenarioId}|${entry.pipeline}|${entry.backend}|${entry.repetition}`,
    entry
  ]));
  const results = [];
  for (const scenarioId of scenarioIds) {
    for (const pipeline of requiredPipelines) {
      for (const backend of requiredBackends) {
        const baseline = byKey.get(`${scenarioId}|${pipeline}|${backend}|1`);
        if (!baseline) continue;
        for (const repetition of [2, 3]) {
          const candidate = byKey.get(`${scenarioId}|${pipeline}|${backend}|${repetition}`);
          if (!candidate) continue;
          results.push(await compareQuadrantPair(baseline, candidate, {
            kind: 'stability',
            caseId: baseline.caseId,
            scenarioId,
            pipeline,
            backend,
            baselineRepetition: 1,
            candidateRepetition: repetition
          }));
        }
      }
    }
  }
  return results;
}

/** Adds failures for generated-product or current aggregate GPU-boundary lint regressions. */
function validateStaticLints(report, aggregateGpuBoundaryLint, failures) {
  if (Array.isArray(report.generatedArtifacts)
      && report.generatedArtifacts.some((entry) => entry.status !== 'pass')) {
    failures.push('Legacy/Experimental 生成产物 lint 未全部通过。');
  }
  if (report.gpuBoundaryLint && report.gpuBoundaryLint.status !== 'pass') {
    failures.push('Sample C++ GPU 边界 lint 未通过。');
  }
  if (report.staticLints
      && Object.values(report.staticLints).some((status) => status !== 'pass')) {
    failures.push('静态生成产物或 GPU 边界 lint 未全部通过。');
  }
  if (isRunComparisonReport(report) && report.lintFailureCount !== 0) {
    failures.push('专属 runner 的生成产物 lint 未通过。');
  }
  if (!report.gpuBoundaryLint && !report.staticLints
      && aggregateGpuBoundaryLint.status !== 'pass') {
    failures.push('严格报告缺少 GPU 边界 lint 证据。');
  }
}

/** Verifies that one quadrant retains its RGBA and metadata evidence files. */
async function validateQuadrantArtifacts(quadrant, failures) {
  for (const [label, artifactPath] of [
    ['RGBA', quadrant.artifacts?.rgbaPath],
    ['metadata', quadrant.artifacts?.metadataPath]
  ]) {
    if (!artifactPath) {
      failures.push(`${quadrant.scenarioId} ${quadrant.pipeline}/${quadrant.backend} 缺少 ${label} 路径。`);
      continue;
    }
    try {
      await fs.access(artifactPath);
    } catch {
      failures.push(`${quadrant.scenarioId} ${quadrant.pipeline}/${quadrant.backend} 的 ${label} 证据不存在：${artifactPath}`);
    }
  }
}

/** Validates one case against its Manifest scenarios and strict report records. */
async function validateCase(example, batchCase, report, aggregateGpuBoundaryLint) {
  const failures = [];
  if (example.status !== 'phase1_required') {
    failures.push(`Manifest 状态为 ${example.status}，不是 phase1_required。`);
  }
  const completeDiagnosticReport = isCompleteSingleCaseDiagnosticReport(report, example);
  if (report.status !== 'pass' && !completeDiagnosticReport) {
    failures.push(`严格报告状态为 ${report.status ?? 'missing'}。`);
  }
  validateThresholds(report, failures);
  validateStaticLints(report, aggregateGpuBoundaryLint, failures);
  if (report.samplePolicy
      && (report.samplePolicy.msaaEnabled === true
        || report.samplePolicy.simulateMsaa === true
        || report.samplePolicy.msaaEmulated === true)) {
    failures.push('严格报告的单采样策略不满足 msaaEnabled=false 且 simulateMsaa=false。');
  }

  const scenarioIds = new Set((example.scenarios ?? []).map((scenario) => scenario.id));
  const fallbackScenarioId = scenarioIds.size === 1 ? [...scenarioIds][0] : null;
  const discoveredQuadrants = isRunComparisonReport(report) || (report.quadrants ?? []).length > 0
    ? []
    : await discoverLegacyQuadrants(batchCase.reportPath, example.id, scenarioIds);
  const normalizedEvidence = normalizeCaseEvidence(
    report, example.id, fallbackScenarioId, discoveredQuadrants);
  let { quadrants, crossComparisons, stabilityComparisons } = normalizedEvidence;
  const expectedQuadrants = scenarioIds.size * 12;
  const expectedCrossComparisons = scenarioIds.size * 12;
  const expectedStabilityComparisons = scenarioIds.size * 8;

  if (crossComparisons.length < expectedCrossComparisons && quadrants.length === expectedQuadrants) {
    crossComparisons = await deriveCrossComparisons(quadrants, scenarioIds);
  }
  if (stabilityComparisons.length !== expectedStabilityComparisons
      && quadrants.length === expectedQuadrants) {
    stabilityComparisons = await deriveStabilityComparisons(quadrants, scenarioIds);
  }
  for (const comparison of [...crossComparisons, ...stabilityComparisons]) {
    const metrics = comparison.metrics;
    if (!metrics) {
      if (comparison.kind === 'stability'
          && comparison.status === 'pass'
          && comparison.differingBytes === 0) {
        continue;
      }
      failures.push('严格报告的跨象限或稳定性记录缺少图像指标。');
      continue;
    }
    if (metrics.normalizedDistancePixelRatio >= expectedThresholds.normalizedDistancePixelRatio
        || metrics.meanAbsoluteRgb > expectedThresholds.meanAbsoluteRgb
        || metrics.p99AbsoluteRgb > expectedThresholds.p99AbsoluteRgb
        || metrics.luminanceSsim < expectedThresholds.luminanceSsim) {
      failures.push('严格报告的跨象限或稳定性记录不满足统一图像阈值。');
    }
  }

  if (quadrants.length !== expectedQuadrants) {
    failures.push(`四象限记录 ${quadrants.length}，预期 ${expectedQuadrants}。`);
  }
  if (crossComparisons.length < expectedCrossComparisons) {
    failures.push(`跨象限记录 ${crossComparisons.length}，至少需要 ${expectedCrossComparisons}。`);
  }
  if (stabilityComparisons.length !== expectedStabilityComparisons) {
    failures.push(`稳定性记录 ${stabilityComparisons.length}，预期 ${expectedStabilityComparisons}。`);
  }
  if (isRunComparisonReport(report)) {
    if (report.caseId !== example.id
        || report.executionCount !== report.runs.length
        || report.oracleComparisonCount !== expectedQuadrants
        || report.crossQuadrantComparisonCount !== crossComparisons.length
        || report.stabilityComparisonCount !== stabilityComparisons.length
        || report.failures?.length !== 0
        || report.singleSamplePolicy?.sampleCount !== 1
        || report.singleSamplePolicy?.msaaEnabled !== false
        || report.singleSamplePolicy?.simulateMsaa !== false) {
      failures.push('专属 runner 的计数、单采样策略或失败清单不满足严格证据契约。');
    }
  }
  for (const entry of [...quadrants, ...crossComparisons, ...stabilityComparisons]) {
    if (entry.status !== 'pass') failures.push('存在状态不是 pass 的门禁记录。');
  }
  for (const scenarioId of scenarioIds) {
    for (let repetition = 1; repetition <= 3; repetition += 1) {
      for (const pipeline of requiredPipelines) {
        for (const backend of requiredBackends) {
          const count = quadrants.filter((entry) => (
            entry.scenarioId === scenarioId
            && entry.repetition === repetition
            && entry.pipeline === pipeline
            && entry.backend === backend
          )).length;
          if (count !== 1) {
            failures.push(`${scenarioId} ${pipeline}/${backend} repeat=${repetition} 记录数为 ${count}。`);
          }
        }
      }
    }
  }
  await Promise.all(quadrants.map((entry) => validateQuadrantArtifacts(entry, failures)));
  return {
    caseId: example.id,
    status: failures.length === 0 ? 'pass' : 'fail',
    reportPath: batchCase.reportPath,
    scenarioCount: scenarioIds.size,
    quadrantCount: quadrants.length,
    crossComparisonCount: crossComparisons.length,
    stabilityComparisonCount: stabilityComparisons.length,
    gpuBoundaryLintSource: report.gpuBoundaryLint || report.staticLints
      ? 'case-report'
      : 'atomic-batch-current-source',
    failures
  };
}

/** Verifies and writes one atomic strict-closure summary for the requested batch. */
async function main() {
  const options = parseArguments(process.argv);
  const manifestPath = requireRepositoryPath(options, 'manifest');
  const batchPath = requireRepositoryPath(options, 'batch');
  const outputPath = requireRepositoryPath(options, 'output');
  const [manifest, batch] = await Promise.all([loadJson(manifestPath), loadJson(batchPath)]);
  if (!Array.isArray(batch.cases) || batch.cases.length < batch.minimumCaseCount
      || batch.minimumCaseCount < 20) {
    throw new Error('Strict closure batch must contain at least 20 cases.');
  }
  const caseIds = batch.cases.map((entry) => entry.caseId);
  if (new Set(caseIds).size !== caseIds.length) {
    throw new Error('Strict closure batch contains duplicate case identifiers.');
  }
  let baselineBatch = batch;
  if (batch.schemaVersion < 2 && batch.baselineStrictCaseIdsPath) {
    const baselineInventory = await loadJson(path.resolve(
      path.dirname(batchPath), '..', batch.baselineStrictCaseIdsPath
    ));
    baselineBatch = {
      ...batch,
      schemaVersion: 2,
      baselineStrictCaseIds: baselineInventory.strictCaseIds,
      baselineStrictCaseIdsSha256: digestCaseIds(baselineInventory.strictCaseIds)
    };
  }
  const baseline = validateNetNewBaseline(baselineBatch, caseIds);
  const lockCounts = validateBatchLocks(batch);
  const [singleSampleSourceLint, gpuBoundaryLint] = await Promise.all([
    validateSingleSampleSources(batch, caseIds),
    lintSampleGpuBoundary(repositoryRoot)
  ]);
  const implementationInventory = await loadActiveImplementationInventories(caseIds);
  const examples = new Map(manifest.examples.map((example) => [example.id, example]));
  const caseResults = [];
  for (const batchCase of batch.cases) {
    const example = examples.get(batchCase.caseId);
    if (!example) throw new Error(`Unknown Manifest case '${batchCase.caseId}'.`);
    if (typeof batchCase.reportPath !== 'string' || batchCase.reportPath.length === 0) {
      caseResults.push({
        caseId: example.id,
        status: 'fail',
        reportPath: null,
        scenarioCount: 0,
        quadrantCount: 0,
        crossComparisonCount: 0,
        stabilityComparisonCount: 0,
        gpuBoundaryLintSource: 'missing-report',
        failures: ['严格批次用例未声明 reportPath，不能进入聚合门禁。']
      });
      continue;
    }
    const report = await loadJson(path.resolve(repositoryRoot, batchCase.reportPath));
    const inventory = implementationInventory.inventories.get(example.id);
    const inventoryFailures = validateImplementationInventory(
      batchCase,
      inventory,
      implementationInventory.duplicateCaseIds.has(example.id)
    );
    const result = await validateCase(example, batchCase, report, gpuBoundaryLint);
    if (inventoryFailures.length > 0) {
      result.status = 'fail';
      result.failures.unshift(...inventoryFailures);
    }
    caseResults.push(result);
  }
  const totals = caseResults.reduce((output, result) => ({
    scenarios: output.scenarios + result.scenarioCount,
    quadrants: output.quadrants + result.quadrantCount,
    crossComparisons: output.crossComparisons + result.crossComparisonCount,
    stabilityComparisons: output.stabilityComparisons + result.stabilityComparisonCount
  }), { scenarios: 0, quadrants: 0, crossComparisons: 0, stabilityComparisons: 0 });
  const totalFailures = [];
  if (singleSampleSourceLint.status !== 'pass') {
    totalFailures.push(
      `单采样策略 lint 发现 ${singleSampleSourceLint.violations.length} 处 MSAA 模拟或超采样实现。`);
  }
  if (gpuBoundaryLint.status !== 'pass') {
    totalFailures.push(
      `当前 Sample C++ GPU 边界 lint 发现 ${gpuBoundaryLint.violationCount} 处违规。`);
  }
  for (const [actual, expected, label] of [
    [totals.scenarios, batch.expectedScenarioCount, 'Manifest 场景'],
    [totals.quadrants, batch.expectedQuadrantCount, '四象限运行'],
    [totals.stabilityComparisons, batch.expectedStabilityComparisonCount, '稳定性比较']
  ]) {
    if (expected != null && actual !== expected) {
      totalFailures.push(`${label}总数 ${actual}，预期 ${expected}。`);
    }
  }
  if (batch.minimumCrossComparisonCount != null
      && totals.crossComparisons < batch.minimumCrossComparisonCount) {
    totalFailures.push(
      `跨象限比较总数 ${totals.crossComparisons}，至少需要 ${batch.minimumCrossComparisonCount}。`);
  }
  const summary = {
    schemaVersion: 1,
    evidenceType: 'strict-example-batch',
    gate: batch.batchId,
    status: caseResults.every((result) => result.status === 'pass')
      && totalFailures.length === 0 ? 'pass' : 'fail',
    minimumCaseCount: batch.minimumCaseCount,
    caseCount: caseResults.length,
    baselineCaseCount: baseline.baselineCaseCount,
    netNewCaseCount: baseline.netNewCaseCount,
    assetLockCount: lockCounts.assetLockCount,
    inputLockCount: lockCounts.inputLockCount,
    oracleLockCount: lockCounts.oracleLockCount,
    singleSampleSourceLint,
    gpuBoundaryLint,
    thresholds: expectedThresholds,
    totals,
    totalFailures,
    caseResults
  };
  await fs.mkdir(path.dirname(outputPath), { recursive: true });
  await fs.writeFile(outputPath, `${JSON.stringify(summary, null, 2)}\n`, 'utf8');
  console.log(JSON.stringify(summary, null, 2));
  if (summary.status !== 'pass') process.exitCode = 1;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
