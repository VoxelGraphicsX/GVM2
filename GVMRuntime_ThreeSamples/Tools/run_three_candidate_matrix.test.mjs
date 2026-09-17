import assert from 'node:assert/strict';
import { promises as fs } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import {
  resolveThreeCaseRandomSeed,
  selectThreeHostExecutable,
  stageThreeScenarioInputs
} from './run_three_candidate_matrix.mjs';

test('resolveThreeCaseRandomSeed adopts one locked Oracle seed for all scenarios', async () => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'gvm-three-oracle-seed-'));
  const caseRoot = path.join(root, 'webgl_buffergeometry_drawrange');
  await fs.mkdir(caseRoot, { recursive: true });
  await fs.writeFile(path.join(caseRoot, 'initial.json'), JSON.stringify({ randomSeed: 407896067 }));
  await fs.writeFile(path.join(caseRoot, 'animated.json'), JSON.stringify({ randomSeed: 407896067 }));

  const seed = await resolveThreeCaseRandomSeed({
    id: 'webgl_buffergeometry_drawrange',
    scenarios: [{ id: 'initial' }, { id: 'animated' }]
  }, root);
  assert.equal(seed, 407896067);
});

test('resolveThreeCaseRandomSeed refuses mixed or absent Oracle seeds', async () => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'gvm-three-oracle-seed-'));
  const caseRoot = path.join(root, 'mixed');
  await fs.mkdir(caseRoot, { recursive: true });
  await fs.writeFile(path.join(caseRoot, 'a.json'), JSON.stringify({ randomSeed: 1 }));
  await fs.writeFile(path.join(caseRoot, 'b.json'), JSON.stringify({ randomSeed: 2 }));

  assert.equal(await resolveThreeCaseRandomSeed({
    id: 'mixed', scenarios: [{ id: 'a' }, { id: 'b' }]
  }, root), null);
  assert.equal(await resolveThreeCaseRandomSeed({
    id: 'missing', scenarios: [{ id: 'a' }]
  }, root), null);
});

test('selectThreeHostExecutable prefers an existing implementation alias', async () => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'gvm-three-host-'));
  const binRoot = path.join(root, 'gvm_three_samples', 'bin');
  await fs.mkdir(binRoot, { recursive: true });
  const executable = path.join(binRoot, 'WebglClipping-legacy');
  await fs.writeFile(executable, 'fixture');

  const selected = await selectThreeHostExecutable(
    root,
    { dslShard: 'WebglClippingRenderSet' },
    'legacy',
    ['WebglClipping']);

  assert.equal(selected, executable);
});

test('selectThreeHostExecutable retains the manifest fallback when no alias exists', async () => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'gvm-three-host-'));
  const selected = await selectThreeHostExecutable(
    root,
    { dslShard: 'UnbuiltShard' },
    'experimental',
    ['MissingAlias']);

  assert.equal(
    selected,
    path.join(root, 'gvm_three_samples', 'bin', 'UnbuiltShard-experimental'));
});

test('stageThreeScenarioInputs copies repository replays and reports missing ones', async () => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'gvm-three-inputs-'));
  const staged = await stageThreeScenarioInputs([{
    scenarios: [
      { inputReplay: 'inputs/webgpu_postprocessing_sobel_disabled.json' },
      { inputReplay: 'inputs/does-not-exist.json' }
    ]
  }], root);
  assert.equal(staged.length, 2);
  assert.equal(staged.find((entry) => entry.source === 'missing').relativePath,
    'inputs/does-not-exist.json');
  assert.equal(
    await fs.stat(path.join(root, 'inputs/webgpu_postprocessing_sobel_disabled.json'))
      .then(() => true),
    true);
});
