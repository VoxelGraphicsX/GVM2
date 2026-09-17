import { createHash } from 'node:crypto';
import { promises as fs } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import assert from 'node:assert/strict';

import { validateThreeBatch } from './validate_three_batch.mjs';

async function writeJson(filePath, value) {
  await fs.mkdir(path.dirname(filePath), { recursive: true });
  await fs.writeFile(filePath, `${JSON.stringify(value, null, 2)}\n`);
}

function makeCaseResult(caseId) {
  const quadrants = [];
  for (let repetition = 1; repetition <= 3; repetition += 1) {
    for (const pipeline of ['legacy', 'experimental']) {
      for (const backend of ['metal', 'vulkan']) {
        quadrants.push({ caseId, scenarioId: 'initial', repetition, pipeline, backend, status: 'pass' });
      }
    }
  }
  return {
    caseId,
    status: 'pass',
    scenarioCount: 1,
    quadrantCount: 12,
    crossComparisonCount: 12,
    stabilityComparisonCount: 8,
    failures: [],
    samplePolicy: { mode: 'single-sample', msaaEnabled: false, simulateMsaa: false },
    quadrants
  };
}

test('validates a 20-case atomic strict batch', async () => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'gvm-three-batch-'));
  const manifestPath = path.join(root, 'manifest.json');
  const batchPath = path.join(root, 'batch.json');
  const summaryPath = path.join(root, 'summary.json');
  const dslRoot = path.join(root, 'Dsl');
  const activeCases = Array.from({ length: 20 }, (_, index) => ({
    caseId: `webgl_case_${index}`,
    dslEntry: `Case${index}Renderer`,
    hostTarget: `Case${index}`
  }));
  await writeJson(manifestPath, {
    examples: activeCases.map(({ caseId }) => ({ id: caseId, status: 'phase1_required' }))
  });
  for (const active of activeCases) {
    await writeJson(path.join(dslRoot, active.caseId, 'implemented-cases.json'), {
      cases: [{ ...active, implementationLevel: 'semantic-complete' }]
    });
  }
  const manifestSha256 = createHash('sha256').update(await fs.readFile(manifestPath)).digest('hex');
  await writeJson(batchPath, {
    manifestSha256,
    activeCases,
    baselineStrictCaseIds: [],
    selectionPolicy: { msaaEnabled: false, simulateMsaa: false }
  });
  await writeJson(summaryPath, {
    status: 'pass',
    caseResults: activeCases.map(({ caseId }) => makeCaseResult(caseId))
  });
  const result = await validateThreeBatch({ manifestPath, batchPath, summaryPath, dslRoot });
  assert.equal(result.status, 'pass', JSON.stringify(result.failures));
  assert.equal(result.activeCaseCount, 20);
});

test('rejects a scaffold and an already-counted baseline case', async () => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'gvm-three-batch-'));
  const manifestPath = path.join(root, 'manifest.json');
  const batchPath = path.join(root, 'batch.json');
  const summaryPath = path.join(root, 'summary.json');
  const dslRoot = path.join(root, 'Dsl');
  const activeCases = Array.from({ length: 20 }, (_, index) => ({
    caseId: `webgl_case_${index}`,
    dslEntry: `Case${index}Renderer`,
    hostTarget: `Case${index}`
  }));
  await writeJson(manifestPath, { examples: activeCases.map(({ caseId }) => ({ id: caseId, status: 'phase1_required' })) });
  await writeJson(path.join(dslRoot, activeCases[0].caseId, 'implemented-cases.json'), {
    cases: [{ ...activeCases[0], implementationLevel: 'scaffolded' }]
  });
  for (const active of activeCases.slice(1)) {
    await writeJson(path.join(dslRoot, active.caseId, 'implemented-cases.json'), {
      cases: [{ ...active, implementationLevel: 'semantic-complete' }]
    });
  }
  const manifestSha256 = createHash('sha256').update(await fs.readFile(manifestPath)).digest('hex');
  await writeJson(batchPath, {
    manifestSha256,
    activeCases,
    baselineStrictCaseIds: [activeCases[1].caseId],
    selectionPolicy: { msaaEnabled: false, simulateMsaa: false }
  });
  await writeJson(summaryPath, {
    status: 'pass',
    caseResults: activeCases.map(({ caseId }) => makeCaseResult(caseId))
  });
  const result = await validateThreeBatch({ manifestPath, batchPath, summaryPath, dslRoot });
  assert.equal(result.status, 'fail');
  assert.ok(result.failures.some((failure) => failure.includes('scaffolded implementation')));
  assert.ok(result.failures.some((failure) => failure.includes('already belongs to baseline')));
});

test('rejects a batch whose declared net-new counts do not match its strict ledger', async () => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'gvm-three-batch-'));
  const manifestPath = path.join(root, 'manifest.json');
  const batchPath = path.join(root, 'batch.json');
  const summaryPath = path.join(root, 'summary.json');
  const dslRoot = path.join(root, 'Dsl');
  const activeCases = Array.from({ length: 20 }, (_, index) => ({
    caseId: `webgl_case_${index}`,
    dslEntry: `Case${index}Renderer`,
    hostTarget: `Case${index}`
  }));
  await writeJson(manifestPath, {
    examples: activeCases.map(({ caseId }) => ({ id: caseId, status: 'phase1_required' }))
  });
  for (const active of activeCases) {
    await writeJson(path.join(dslRoot, active.caseId, 'implemented-cases.json'), {
      cases: [{ ...active, implementationLevel: 'semantic-complete' }]
    });
  }
  const manifestSha256 = createHash('sha256').update(await fs.readFile(manifestPath)).digest('hex');
  await writeJson(batchPath, {
    manifestSha256,
    activeCases,
    cases: activeCases,
    baselineStrictCaseCount: 19,
    baselineStrictCaseIds: [],
    minimumNetNewCaseCount: 20,
    selectionPolicy: { msaaEnabled: false, simulateMsaa: false }
  });
  await writeJson(summaryPath, {
    status: 'pass',
    baselineCaseCount: 19,
    netNewCaseCount: 19,
    caseCount: 19,
    caseResults: activeCases.map(({ caseId }) => makeCaseResult(caseId))
  });
  const result = await validateThreeBatch({ manifestPath, batchPath, summaryPath, dslRoot });
  assert.equal(result.status, 'fail');
  assert.ok(result.failures.some((failure) => failure.includes('baselineStrictCaseCount')));
  assert.ok(result.failures.some((failure) => failure.includes('summary.caseCount')));
  assert.ok(result.failures.some((failure) => failure.includes('summary.netNewCaseCount')));
});

test('resolves a schema-1 baselineStrictCaseIdsPath relative to the batch manifest', async () => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'gvm-three-batch-'));
  const manifestDirectory = path.join(root, 'Manifest');
  const manifestPath = path.join(manifestDirectory, 'manifest.json');
  const batchPath = path.join(manifestDirectory, 'batch.json');
  const baselinePath = path.join(manifestDirectory, 'baseline.json');
  const summaryPath = path.join(root, 'summary.json');
  const dslRoot = path.join(root, 'Dsl');
  const activeCases = Array.from({ length: 20 }, (_, index) => ({
    caseId: `webgl_path_case_${index}`,
    dslEntry: `PathCase${index}Renderer`,
    hostTarget: `PathCase${index}`
  }));
  await writeJson(manifestPath, {
    examples: activeCases.map(({ caseId }) => ({ id: caseId, status: 'phase1_required' }))
  });
  for (const active of activeCases) {
    await writeJson(path.join(dslRoot, active.caseId, 'implemented-cases.json'), {
      cases: [{ ...active, implementationLevel: 'semantic-complete' }]
    });
  }
  await writeJson(baselinePath, {
    strictCaseIds: ['webgl_baseline_case'],
    strictCaseCount: 1
  });
  const manifestSha256 = createHash('sha256').update(await fs.readFile(manifestPath)).digest('hex');
  await writeJson(batchPath, {
    manifestSha256,
    activeCases,
    baselineStrictCaseCount: 1,
    baselineStrictCaseIdsPath: 'baseline.json',
    selectionPolicy: { msaaEnabled: false, simulateMsaa: false }
  });
  await writeJson(summaryPath, {
    status: 'pass',
    baselineCaseCount: 1,
    caseResults: activeCases.map(({ caseId }) => makeCaseResult(caseId))
  });
  const result = await validateThreeBatch({ manifestPath, batchPath, summaryPath, dslRoot });
  assert.equal(result.status, 'pass', JSON.stringify(result.failures));
});

test('accepts the schema-1 strict closure shape with case-only active entries', async () => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'gvm-three-batch-'));
  const manifestDirectory = path.join(root, 'Manifest');
  const manifestPath = path.join(manifestDirectory, 'manifest.json');
  const batchPath = path.join(manifestDirectory, 'batch.json');
  const baselinePath = path.join(manifestDirectory, 'baseline.json');
  const summaryPath = path.join(root, 'summary.json');
  const dslRoot = path.join(root, 'Dsl');
  const activeCases = Array.from({ length: 20 }, (_, index) => ({
    caseId: `webgl_schema1_case_${index}`,
    reportPath: `reports/${index}.json`
  }));
  const manifest = {
    examples: activeCases.map(({ caseId }) => ({ id: caseId, status: 'phase1_required' }))
  };
  await writeJson(manifestPath, manifest);
  await writeJson(baselinePath, {
    strictCaseIds: ['webgl_existing_case'],
    strictCaseCount: 1
  });
  for (const [index, active] of activeCases.entries()) {
    await writeJson(path.join(dslRoot, active.caseId, 'implemented-cases.json'), {
      cases: [{
        ...active,
        dslEntry: `Schema1Case${index}Renderer`,
        hostTarget: `Schema1Case${index}`,
        implementationLevel: 'semantic-complete'
      }]
    });
  }
  const manifestSha256 = createHash('sha256')
    .update(await fs.readFile(manifestPath))
    .digest('hex');
  await writeJson(batchPath, {
    manifestSha256,
    baselineStrictCaseCount: 1,
    baselineStrictCaseIdsPath: 'baseline.json',
    activeCases,
    cases: activeCases,
    selectionPolicy: { msaaEnabled: false, simulateMsaa: false }
  });
  await writeJson(summaryPath, {
    status: 'pass',
    caseCount: activeCases.length,
    baselineCaseCount: 1,
    netNewCaseCount: activeCases.length,
    caseResults: activeCases.map(({ caseId }) => makeCaseResult(caseId))
  });
  const result = await validateThreeBatch({ manifestPath, batchPath, summaryPath, dslRoot });
  assert.equal(result.status, 'pass', JSON.stringify(result.failures));
  assert.equal(result.activeCaseCount, 20);
});
