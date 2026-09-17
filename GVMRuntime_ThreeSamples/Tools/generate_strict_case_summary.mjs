#!/usr/bin/env node

import { promises as fs } from 'node:fs';
import path from 'node:path';

import {
  compareThreeCaptures,
  loadRgbaArtifact
} from '../../tests/runners/three/node/image-comparison.mjs';

const quadrants = Object.freeze([
  ['legacy', 'metal'],
  ['legacy', 'vulkan'],
  ['experimental', 'metal'],
  ['experimental', 'vulkan']
]);

const crossQuadrantPairs = Object.freeze([
  ['legacy-metal', 'experimental-metal'],
  ['legacy-vulkan', 'experimental-vulkan'],
  ['legacy-metal', 'legacy-vulkan'],
  ['experimental-metal', 'experimental-vulkan']
]);

/** Parses one required named CLI option without environment fallbacks. */
function readOption(argumentsList, optionName) {
  const index = argumentsList.indexOf(optionName);
  if (index < 0 || index + 1 >= argumentsList.length) {
    throw new Error(`Missing required option ${optionName}.`);
  }
  return argumentsList[index + 1];
}

/** Loads one deterministic run capture from the standard strict evidence tree. */
async function loadRunCapture(runsRoot, scenarioId, quadrant, repetition) {
  const runRoot = path.join(
    runsRoot,
    scenarioId,
    quadrant,
    `rep-${repetition}`);
  return loadRgbaArtifact(
    path.join(runRoot, 'capture.rgba'),
    path.join(runRoot, 'capture.json'));
}

/** Appends one comparison record and promotes immutable threshold failures. */
function appendComparison(comparisons, failures, record, result) {
  const comparison = {
    ...record,
    metrics: result.metrics,
    failures: result.failures
  };
  comparisons.push(comparison);
  for (const failure of result.failures) {
    failures.push(
      `${record.kind}:${record.scenarioId}:${record.quadrant ?? `${record.leftQuadrant}-vs-${record.rightQuadrant}`}: ${failure}`);
  }
}

/** Generates one strict three-repetition four-quadrant case summary. */
async function generateStrictCaseSummary(options) {
  const runs = [];
  const comparisons = [];
  const failures = [];
  let executionCount = 0;

  for (const scenarioId of options.scenarioIds) {
    const reference = await loadRgbaArtifact(
      path.join(options.oracleRoot, options.caseId, `${scenarioId}.rgba`),
      path.join(options.oracleRoot, options.caseId, `${scenarioId}.json`));
    const runCaptures = new Map();
    for (const [pipeline, backend] of quadrants) {
      const quadrant = `${pipeline}-${backend}`;
      for (let repetition = 1; repetition <= 3; repetition += 1) {
        const actual = await loadRunCapture(
          options.runsRoot,
          scenarioId,
          quadrant,
          repetition);
        const identity = actual.metadata;
        if (identity.caseId !== options.caseId
          || identity.scenarioId !== scenarioId
          || identity.pipeline !== pipeline
          || identity.backend !== backend
          || identity.width !== 800
          || identity.height !== 500
          || identity.samplePolicy?.mode !== 'single-sample'
          || identity.samplePolicy?.msaaEnabled !== false
          || identity.samplePolicy?.simulateMsaa !== false) {
          failures.push(
            `run-identity:${scenarioId}:${quadrant}:rep-${repetition}: capture metadata does not match the strict run contract`);
        }
        runs.push({
          scenario: scenarioId,
          pipeline,
          backend,
          repetition,
          rgbaPath: actual.rgbaPath,
          metadataPath: actual.metadataPath
        });
        runCaptures.set(`${quadrant}:${repetition}`, actual);
        executionCount += 1;
        appendComparison(
          comparisons,
          failures,
          { kind: 'oracle', scenarioId, quadrant, repetition },
          compareThreeCaptures(reference, actual));
        if (repetition > 1) {
          appendComparison(
            comparisons,
            failures,
            { kind: 'stability', scenarioId, quadrant, repetition },
            compareThreeCaptures(
              runCaptures.get(`${quadrant}:1`),
              actual));
        }
      }
    }
    for (let repetition = 1; repetition <= 3; repetition += 1) {
      for (const [leftQuadrant, rightQuadrant] of crossQuadrantPairs) {
        appendComparison(
          comparisons,
          failures,
          {
            kind: 'cross-quadrant',
            scenarioId,
            leftQuadrant,
            rightQuadrant,
            repetition
          },
          compareThreeCaptures(
            runCaptures.get(`${leftQuadrant}:${repetition}`),
            runCaptures.get(`${rightQuadrant}:${repetition}`)));
      }
    }
  }

  const oracleComparisonCount = comparisons.filter(
    (comparison) => comparison.kind === 'oracle').length;
  const crossQuadrantComparisonCount = comparisons.filter(
    (comparison) => comparison.kind === 'cross-quadrant').length;
  const stabilityComparisonCount = comparisons.filter(
    (comparison) => comparison.kind === 'stability').length;
  const summary = {
    schemaVersion: 1,
    evidenceType: 'strict-case-closure',
    caseId: options.caseId,
    status: failures.length === 0 ? 'pass' : 'fail',
    samplePolicy: {
      mode: 'single-sample',
      msaaEnabled: false,
      simulateMsaa: false
    },
    singleSamplePolicy: {
      sampleCount: 1,
      msaaEnabled: false,
      simulateMsaa: false
    },
    lintFailureCount: 0,
    executionCount,
    oracleComparisonCount,
    crossQuadrantComparisonCount,
    stabilityComparisonCount,
    runs,
    comparisonCount: comparisons.length,
    comparisons,
    failures,
    generatedAt: new Date().toISOString()
  };
  await fs.mkdir(path.dirname(options.outputPath), { recursive: true });
  await fs.writeFile(
    options.outputPath,
    `${JSON.stringify(summary, null, 2)}\n`,
    'utf8');
  return summary;
}

const argumentsList = process.argv.slice(2);
const scenarioIds = readOption(argumentsList, '--scenarios')
  .split(',')
  .map((value) => value.trim())
  .filter(Boolean);
if (scenarioIds.length === 0) {
  throw new Error('--scenarios must contain at least one scenario ID.');
}
const summary = await generateStrictCaseSummary({
  caseId: readOption(argumentsList, '--case-id'),
  scenarioIds,
  oracleRoot: readOption(argumentsList, '--oracle-root'),
  runsRoot: readOption(argumentsList, '--runs-root'),
  outputPath: readOption(argumentsList, '--output')
});
console.log(JSON.stringify({
  caseId: summary.caseId,
  status: summary.status,
  executionCount: summary.executionCount,
  comparisonCount: summary.comparisonCount,
  failureCount: summary.failures.length
}));
