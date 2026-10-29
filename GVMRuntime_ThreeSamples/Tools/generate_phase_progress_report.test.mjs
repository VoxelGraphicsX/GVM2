import assert from 'node:assert/strict';
import { mkdtemp, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import {
  collectCaseIds,
  collectReportCaseIds,
  computeMetrics,
  createReportRow,
  addDeclaredReportEvidence,
  getCaseImplementationLevel,
  hasCaseImplementation,
  hasHistoricalStrictEvidence,
  isCurrentGateReport,
  isCurrentStrictBatchCase,
  isCurrentStrictReport,
  parseArguments,
  renderAtomicBatchSection,
  resolveImplementationShard,
  updateReportHtml
} from './generate_phase_progress_report.mjs';

/** Creates one minimal current-schema strict report. */
function makeStrictReport() {
  const quadrants = Array.from({ length: 12 }, () => ({ status: 'pass' }));
  return {
    status: 'pass',
    repeatCount: 3,
    runCount: 12,
    expectedRunCount: 12,
    quadrants,
    crossComparisons: Array.from({ length: 12 }, () => ({ status: 'pass' })),
    stabilityComparisons: Array.from({ length: 8 }, () => ({ status: 'pass' })),
    generatedArtifacts: [{ status: 'pass' }, { status: 'pass' }],
    gpuBoundaryLint: { status: 'pass' }
  };
}

/** Creates one minimal supported manifest example. */
function makeExample() {
  return {
    id: 'webgpu_example',
    category: 'webgpu',
    upstreamPath: 'examples/webgpu_example.html',
    status: 'phase1_required',
    exclusion: null,
    capabilityAudit: { requiredCapabilities: ['compute_dispatch'], evidence: [] },
    renderSetPolicy: 'not-required',
    renderSetReasons: [],
    sceneRoots: [{ name: 'scene', renderSetRuntimeInstanceCount: 0 }],
    renderableObjectCount: 1,
    containsInstancing: false,
    containsHierarchy: false,
    containsLod: false,
    containsDynamicObjects: false,
    containsMultipleMaterials: false,
    scenePasses: [{ name: 'main' }],
    screenPasses: [],
    renderSetType: null,
    dslShard: 'WebgpuExample',
    scenarios: [{ id: 'initial', frame: 0 }],
    deferredEvidence: null
  };
}

test('parseArguments rejects implicit and duplicate report paths', () => {
  assert.deepEqual(parseArguments(['node', 'tool.mjs', '--manifest', '/manifest.json']), {
    manifest: '/manifest.json'
  });
  assert.throws(() => parseArguments([
    'node', 'tool.mjs', '--manifest', '/a', '--manifest', '/b'
  ]), /Duplicate/u);
});

test('collectCaseIds handles nested heterogeneous report records', () => {
  assert.deepEqual([...collectCaseIds({
    selectedCases: [{ id: 'webgl_cube' }, { id: 'misc_uv_tests' }, { id: 'svg_lines' }],
    caseId: 'webgpu_test'
  })].sort(), [
    'misc_uv_tests',
    'svg_lines',
    'webgl_cube',
    'webgpu_test'
  ]);
});

test('collectReportCaseIds rejects a summary selected for a different example', () => {
  const report = {
    status: 'pass',
    selectedCases: ['webgl_geometry_extrude_shapes'],
    quadrants: [{ caseId: 'webgl_geometry_shapes', status: 'pass' }]
  };
  assert.deepEqual([...collectReportCaseIds(report)], ['webgl_geometry_extrude_shapes']);
});

test('collectReportCaseIds falls back to per-entry identity for old summaries', () => {
  const report = {
    status: 'pass',
    quadrants: [
      { caseId: 'webgl_old_case', status: 'pass' },
      { caseId: 'webgl_old_case', status: 'pass' }
    ]
  };
  assert.deepEqual([...collectReportCaseIds(report)], ['webgl_old_case']);
});

test('isCurrentStrictReport requires all four current gate dimensions', () => {
  const report = makeStrictReport();
  assert.equal(isCurrentStrictReport(report), true);
  report.stabilityComparisons[0].status = 'fail';
  assert.equal(isCurrentStrictReport(report), false);
});

test('texture-case fixture schema counts only a complete strict matrix', () => {
  const primaryQuadrants = [
    { caseId: 'webgl_texture_case', scenario: 'initial', status: 'pass' },
    { caseId: 'webgl_texture_case', scenario: 'initial', status: 'pass' },
    { caseId: 'webgl_texture_case', scenario: 'initial', status: 'pass' },
    { caseId: 'webgl_texture_case', scenario: 'initial', status: 'pass' }
  ];
  const report = {
    gate: 'three-r185-phase1-texture-cases',
    status: 'pass',
    stabilityRunCount: 3,
    inputValidation: { status: 'pass' },
    generatedArtifacts: { status: 'pass' },
    quadrants: primaryQuadrants,
    stabilityComparisons: primaryQuadrants.map(() => ({
      status: 'pass',
      requiredRunCount: 3
    })),
    comparisons: Array.from({ length: 4 }, () => ({ status: 'pass' })),
    oracleComparisons: primaryQuadrants.map(() => ({ status: 'pass' }))
  };
  assert.equal(hasHistoricalStrictEvidence([{ report }]), true);
  report.oracleComparisons[0].status = 'fail';
  assert.equal(hasHistoricalStrictEvidence([{ report }]), false);
});

test('declared strict report takes precedence over stale scanned evidence', async () => {
  const temporaryDirectory = await mkdtemp(path.join(os.tmpdir(), 'gvm-report-evidence-'));
  const reportPath = path.join(temporaryDirectory, 'summary.json');
  await writeFile(reportPath, JSON.stringify(makeStrictReport()));
  const inventories = new Map([[
    'WebgpuExample',
    new Map([[
      'webgpu_example',
      {
        caseId: 'webgpu_example',
        implementationLevel: 'strict-pass',
        strictReport: reportPath
      }
    ]])
  ]]);
  const evidence = new Map([['webgpu_example', [{
    relativePath: 'build/stale/summary.json',
    report: makeStrictReport(),
    passing: true,
    repeat: 3,
    quadrants: 12,
    stability: 8,
    score: 100
  }]]]);
  await addDeclaredReportEvidence(inventories, evidence);
  assert.equal(evidence.get('webgpu_example').at(-1).relativePath, path.relative(
    process.cwd(), reportPath));
  assert.ok(evidence.get('webgpu_example').at(-1).score > 2_000_000);
});

test('historical coverage schema with explicit three-repeat matrix counts as strict', () => {
  const report = {
    status: 'pass',
    quadrants: [
      ...[1, 2, 3].flatMap((repetition) => [
        { repetition, status: 'pass' },
        { repetition, status: 'pass' },
        { repetition, status: 'pass' },
        { repetition, status: 'pass' }
      ])
    ],
    crossComparisons: Array.from({ length: 12 }, () => ({ status: 'pass' })),
    stabilityComparisons: Array.from({ length: 8 }, () => ({ status: 'pass' })),
    coverage: {
      gateComplete: true,
      matrixComplete: true,
      selectionComplete: true,
      selectedPipelines: ['legacy', 'experimental'],
      selectedBackends: ['metal', 'vulkan']
    }
  };
  assert.equal(hasHistoricalStrictEvidence([{ report }]), true);
  report.quadrants[0].status = 'fail';
  assert.equal(hasHistoricalStrictEvidence([{ report }]), false);
});

test('complete single-case diagnostic matrix counts as strict evidence', () => {
  const makeEntries = (count) => Array.from({ length: count }, () => ({ status: 'pass' }));
  const report = {
    status: 'diagnostic-pass',
    selectedCases: ['webgl_math_orientation_transform'],
    quadrants: makeEntries(48),
    crossComparisons: makeEntries(48),
    stabilityComparisons: makeEntries(32),
    coverage: {
      caseId: 'webgl_math_orientation_transform',
      scenarioCount: 4,
      quadrantCount: 48,
      crossComparisonCount: 48,
      stabilityComparisonCount: 32,
      oracleFailures: 0,
      crossFailures: 0,
      stabilityFailures: 0
    },
    generatedArtifacts: [{ status: 'pass' }],
    gpuBoundaryLint: { status: 'pass' }
  };
  assert.equal(hasHistoricalStrictEvidence([{ report }]), true);
  report.coverage.oracleFailures = 1;
  assert.equal(hasHistoricalStrictEvidence([{ report }]), false);
});

test('strict-case-closure ledger with single-sample policy counts as strict', () => {
  const comparisons = [
    ...Array.from({ length: 24 }, () => ({ kind: 'oracle', failures: [] })),
    ...Array.from({ length: 16 }, () => ({ kind: 'stability', failures: [] })),
    ...Array.from({ length: 8 }, () => ({ kind: 'cross-quadrant', failures: [] }))
  ];
  const report = {
    evidenceType: 'strict-case-closure',
    status: 'pass',
    samplePolicy: { mode: 'single-sample', msaaEnabled: false, simulateMsaa: false },
    executionCount: 24,
    comparisonCount: comparisons.length,
    comparisons
  };
  assert.equal(hasHistoricalStrictEvidence([{ report }]), true);
  report.samplePolicy.msaaEnabled = true;
  assert.equal(hasHistoricalStrictEvidence([{ report }]), false);
});

test('aggregate strict summary with explicit three-repeat matrices counts as strict', () => {
  const makeResults = (count) => Array.from({ length: count }, () => ({ failures: [] }));
  const report = {
    implementationLevel: 'strict-pass',
    status: 'pass',
    scenarioCount: 3,
    quadrantCount: 4,
    repeats: 3,
    quadrants: [
      { pipeline: 'legacy', backend: 'metal' },
      { pipeline: 'legacy', backend: 'vulkan' },
      { pipeline: 'experimental', backend: 'metal' },
      { pipeline: 'experimental', backend: 'vulkan' }
    ],
    oracleResults: makeResults(36),
    stabilityResults: makeResults(24),
    crossResults: makeResults(54),
    evidence: {
      oracleComparisons: 36,
      stabilityComparisons: 24,
      crossQuadrantComparisons: 54,
      failureCount: 0
    },
    samplePolicy: {
      mode: 'single-sample',
      msaaEnabled: false,
      simulateMsaa: false
    }
  };
  assert.equal(hasHistoricalStrictEvidence([{ report }]), true);
  report.oracleResults[0].failures.push('mismatch');
  assert.equal(hasHistoricalStrictEvidence([{ report }]), false);
  report.oracleResults[0].failures = [];
  delete report.scenarioCount;
  assert.equal(hasHistoricalStrictEvidence([{ report }]), false);
});

test('isCurrentStrictBatchCase requires a 20-case atomic closure contract', () => {
  const report = {
    evidenceType: 'strict-example-batch',
    status: 'pass',
    minimumCaseCount: 20,
    caseCount: 20
  };
  const caseResult = {
    caseId: 'webgpu_example',
    status: 'pass',
    scenarioCount: 2,
    quadrantCount: 24,
    crossComparisonCount: 24,
    stabilityComparisonCount: 16,
    failures: []
  };
  assert.equal(isCurrentStrictBatchCase(report, caseResult), true);
  caseResult.quadrantCount = 12;
  assert.equal(isCurrentStrictBatchCase(report, caseResult), false);
});

test('strict batch case records retain their completed gate identity', () => {
  const record = {
    relativePath: 'build/three-r185-atomic/summary.json',
    report: { status: 'pass', evidenceType: 'strict-example-batch' },
    strictBatchCase: true,
    passing: true,
    repeat: 3,
    quadrants: 24,
    runs: 24,
    stability: 16,
    score: 100000
  };
  assert.equal(hasHistoricalStrictEvidence([record]), true);
  const row = createReportRow(
    makeExample(),
    0,
    new Set(['WebgpuExample']),
    [record]);
  assert.equal(row.implementationStage, 'strict_pass');
});

test('createReportRow promotes only implemented examples with strict evidence', () => {
  const report = makeStrictReport();
  const records = [{
    relativePath: 'build/three-r185-webgpu-example-formal/summary.json',
    report,
    repeat: 3,
    quadrants: 12,
    runs: 0,
    stability: 8,
    score: 4000
  }];
  const row = createReportRow(makeExample(), 0, new Set(['WebgpuExample']), records);
  assert.equal(row.implementationStage, 'strict_pass');
  assert.equal(row.codeExists, true);
  assert.match(row.issue, /四象限/u);
});

test('current failed gate evidence overrides an old runner pass label', () => {
  const oldPass = {
    status: 'pass',
    quadrants: [{ status: 'pass' }]
  };
  const currentFailure = {
    status: 'fail',
    evidenceType: 'strict-case-closure',
    quadrants: [{ status: 'fail' }],
    crossComparisons: [],
    stabilityComparisons: []
  };
  assert.equal(isCurrentGateReport(currentFailure), true);
  const row = createReportRow(
    makeExample(),
    0,
    new Set(['WebgpuExample']),
    [
      { relativePath: 'build/old/summary.json', report: oldPass, passing: true },
      { relativePath: 'build/current/summary.json', report: currentFailure, passing: false }
    ]);
  assert.equal(row.implementationStage, 'implemented_pending');
});

test('shared shard inventory does not mark planned but unsupported cases as implemented', () => {
  const example = makeExample();
  const directories = new Set(['WebgpuExample']);
  const inventories = new Map([['WebgpuExample', new Set(['webgpu_other'])]]);
  assert.equal(hasCaseImplementation(example, directories, inventories), false);
  assert.equal(createReportRow(example, 0, directories, [], inventories).codeExists, false);
  inventories.get('WebgpuExample').add('webgpu_example');
  assert.equal(hasCaseImplementation(example, directories, inventories), true);
});

test('dedicated inventory resolves a case that still names a planning shard', () => {
  const example = makeExample();
  const directories = new Set(['Phase1Planned', 'WebgpuExample']);
  example.dslShard = 'Phase1Planned';
  const inventories = new Map([['WebgpuExample', new Map([[
    'webgpu_example',
    {
      caseId: 'webgpu_example',
      implementationLevel: 'semantic-complete',
      dslEntry: 'WebgpuExample',
      hostTarget: 'WebgpuExample',
      scenePasses: ['WebgpuExampleMainPass'],
      renderSetType: null
    }
  ]])]]);
  assert.equal(resolveImplementationShard(example, directories, inventories), 'WebgpuExample');
  assert.equal(hasCaseImplementation(example, directories, inventories), true);
  assert.equal(getCaseImplementationLevel(example, inventories), 'semantic-complete');
});

test('schemaVersion 2 scaffold inventory stays below semantic implementation', () => {
  const example = makeExample();
  const directories = new Set(['WebgpuExample']);
  const inventories = new Map([['WebgpuExample', new Map([[
    'webgpu_example',
    {
      caseId: 'webgpu_example',
      implementationLevel: 'scaffolded',
      dslEntry: 'WebgpuExample',
      hostTarget: 'WebgpuExample',
      scenePasses: ['WebgpuExampleMainPass'],
      renderSetType: null
    }
  ]])]]);
  assert.equal(getCaseImplementationLevel(example, inventories), 'scaffolded');
  const row = createReportRow(example, 0, directories, [], inventories);
  assert.equal(row.implementationStage, 'scaffolded');
  assert.equal(row.codeExists, true);
});

test('computeMetrics keeps audit and implementation totals separate', () => {
  const manifest = {
    counts: {
      total: 588,
      excludedUpstream: 77,
      deferredMissingCapability: 27,
      phase1Required: 484,
      auditPopulation: 511
    }
  };
  const rows = [
    { implementationStage: 'strict_pass' },
    { implementationStage: 'legacy_pass' },
    { implementationStage: 'implemented_pending' },
    { implementationStage: 'scaffolded' },
    { implementationStage: 'required_unimplemented' },
    { implementationStage: 'deferred' },
    { implementationStage: 'excluded' }
  ];
  const metrics = computeMetrics(manifest, rows);
  assert.equal(metrics.implemented, 3);
  assert.equal(metrics.codeAvailable, 4);
  assert.equal(metrics.scaffolded, 1);
  assert.equal(metrics.runnerPass, 2);
  assert.equal(metrics.strictPass, 1);
  assert.equal(metrics.remainingStrict, 483);
});

test('updateReportHtml refreshes summary and all embedded task rows', () => {
  const template = `<section><!-- generated-summary:start -->old<!-- generated-summary:end --></section>
const rows=[];
const labels={};
<div><h3>完成层级</h3><ul class="risk-list"><li>old</li></ul></div>`;
  const manifest = {
    counts: {
      total: 588,
      excludedUpstream: 77,
      deferredMissingCapability: 27,
      phase1Required: 484,
      auditPopulation: 511
    }
  };
  const rows = [{ id: 'webgpu_example', implementationStage: 'strict_pass' }];
  const output = updateReportHtml(template, rows, computeMetrics(manifest, rows));
  assert.match(output, /当前三次稳定证据完整/u);
  assert.equal(output.includes('const rows=[{"id":"webgpu_example"'), true);
  assert.match(output, /真实 DSL\/C\+\+ 代码共 1 项/u);
});

test('updateReportHtml refreshes the current Chinese risk counts', () => {
  const template = '<section><!-- generated-summary:start -->old<!-- generated-summary:end --></section>'
    + '<section><!-- generated-overview:start -->old<!-- generated-overview:end --></section>'
    + '<section><!-- generated-wave:start -->old<!-- generated-wave:end --></section>'
    + '<section><!-- generated-atomic-batch:start -->old<!-- generated-atomic-batch:end --></section>'
    + '<section><!-- generated-capability:start -->old<!-- generated-capability:end --></section>'
    + '<section><!-- generated-metrics:start -->old<!-- generated-metrics:end --></section>'
    + '<section><!-- generated-table:start -->old<!-- generated-table:end --></section>'
    + 'const rows=[];\nconst labels={};'
    + '<ol class="risk-list">'
    + '<li><b>主体移植量仍大：</b>483 个必做项中已有代码 135 个，尚有 348 个没有对应 DSL shard。</li>'
    + '<li><b>共享占位骨架技术债：</b>当前仅骨架计数为 1；旧内容。</li>'
    + '</ol>';
  const manifest = {
    counts: {
      total: 588,
      excludedUpstream: 77,
      deferredMissingCapability: 28,
      phase1Required: 483,
      auditPopulation: 511
    }
  };
  const rows = [
    { id: 'webgpu_example', implementationStage: 'scaffolded' },
    { id: 'webgpu_strict', implementationStage: 'strict_pass' }
  ];
  const output = updateReportHtml(template, rows, computeMetrics(manifest, rows));
  assert.match(output, /483 个必做项中已有代码 2 个，尚有 0 个没有对应 DSL shard/u);
  assert.match(output, /共享占位骨架技术债：<\/b>当前仅骨架计数为 1/u);
});

test('atomic batch renderer records explicit summary path and deterministic replacement', () => {
  const summary = {
    evidenceType: 'strict-example-batch',
    status: 'pass',
    baselineCaseCount: 57,
    netNewCaseCount: 20,
    caseResults: Array.from({ length: 20 }, (_, index) => ({
      caseId: `webgpu_case_${index}`,
      scenarioCount: 1,
      quadrantCount: 12,
      crossComparisonCount: 12,
      stabilityComparisonCount: 8,
      reportPath: `build/case-${index}/summary.json`
    })),
    totals: {
      scenarios: 20,
      quadrants: 240,
      crossComparisons: 240,
      stabilityComparisons: 160
    },
    gpuBoundaryLint: { filesScanned: 1, violationCount: 0 },
    singleSampleSourceLint: { violations: [] }
  };
  const output = renderAtomicBatchSection(summary, {
    summaryPath: 'build/current-wave2.json',
    batchDefinition: {
      replacementDecisions: [{
        blockedCaseId: 'webgl_blocked',
        replacementCaseId: 'webgpu_replacement',
        reasonCode: 'wireframe_coverage',
        evidence: 'build/blocked.json'
      }]
    }
  });
  assert.match(output, /build\/current-wave2\.json/u);
  assert.match(output, /webgl_blocked.*webgpu_replacement/u);
});

test('atomic batch renderer distinguishes completed case evidence from batch-pending status', () => {
  const summary = {
    evidenceType: 'strict-example-batch',
    status: 'pending',
    baselineCaseCount: 77,
    netNewCaseCount: 20,
    caseResults: [
      {
        caseId: 'webgl_math_orientation_transform',
        status: 'formal-evidence-complete-pending-atomic-batch',
        scenarioCount: 4,
        quadrantCount: 48,
        crossComparisonCount: 48,
        stabilityComparisonCount: 32,
        reportPath: 'build/orientation/summary.json',
        failures: ['等待整批原子闭环']
      },
      ...Array.from({ length: 19 }, (_, index) => ({
        caseId: `webgpu_pending_${index}`,
        status: 'pending',
        scenarioCount: 1,
        quadrantCount: 4,
        crossComparisonCount: 0,
        stabilityComparisonCount: 0,
        reportPath: 'build/pending/summary.json'
      }))
    ],
    totals: { scenarios: 23, quadrants: 124, crossComparisons: 48, stabilityComparisons: 32 },
    gpuBoundaryLint: { filesScanned: 1, violationCount: 0 },
    singleSampleSourceLint: { violations: [] }
  };
  const output = renderAtomicBatchSection(summary);
  assert.match(output, /<code>webgl_math_orientation_transform<\/code><\/td>[\s\S]*?chip implemented_pending[^>]*>本项证据完成／待原子批次/u);
});
