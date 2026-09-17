import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { promises as fs } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import {
  buildExpectedSnapshot,
  createDiagnosticManifest,
  parseArguments,
  parseRepeatCount,
  pointsWavesScenarios,
  validateExpectedFields,
  validateGeneratedArtifacts,
  validateManifestContract
} from './run_webgl_points_waves.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');

test('scenario matrix locks initial, animated, and deterministic camera captures', () => {
  assert.deepEqual(pointsWavesScenarios.map((scenario) => ({
    id: scenario.id,
    kind: scenario.kind,
    frame: scenario.frame,
    inputReplay: scenario.inputReplay
  })), [
    { id: 'initial', kind: 'initial-frame', frame: 0, inputReplay: null },
    { id: 'animated', kind: 'fixed-frame', frame: 60, inputReplay: null },
    {
      id: 'camera-input',
      kind: 'input-replay',
      frame: 61,
      inputReplay: 'inputs/webgl_points_waves_camera.json'
    }
  ]);
  assert.equal(pointsWavesScenarios[2].cameraX, 95.84220064142759);
  assert.equal(pointsWavesScenarios[2].cameraY, 0);
});

test('argument parser and repeat gate reject ambiguous invocations', () => {
  assert.deepEqual(parseArguments([
    'node', 'fixture', '--output-dir', 'out', '--repeat', '3'
  ]), { 'output-dir': 'out', repeat: '3' });
  assert.throws(
    () => parseArguments(['node', 'fixture', '--output-dir']),
    /Expected --option value pair/u);
  assert.throws(
    () => parseArguments([
      'node', 'fixture', '--output-dir', 'a', '--output-dir', 'b'
    ]),
    /Duplicate --output-dir/u);
  assert.equal(parseRepeatCount(undefined), 3);
  assert.equal(parseRepeatCount('4'), 4);
  assert.throws(() => parseRepeatCount('2'), /at least 3/u);
  assert.throws(() => parseRepeatCount('3.5'), /at least 3/u);
});

test('snapshot contract locks ordinary geometry and one single-sample pass', () => {
  const initial = buildExpectedSnapshot(pointsWavesScenarios[0]);
  const animated = buildExpectedSnapshot(pointsWavesScenarios[1]);
  const camera = buildExpectedSnapshot(pointsWavesScenarios[2]);
  assert.equal(initial.renderSetPolicy, 'not-required');
  assert.equal(initial.sceneRenderSetCount, 0);
  assert.equal(initial.renderableObjectCount, 1);
  assert.equal(initial.logicalPointCount, 2500);
  assert.equal(initial.expandedVertexCount, 7500);
  assert.equal(initial.explicitIndexCount, 7500);
  assert.equal(initial.scenePassCount, 1);
  assert.equal(initial.logicalScenePassCount, 1);
  assert.equal(initial.screenPassCount, 0);
  assert.equal(initial.drawCommandCount, 1);
  assert.equal(initial.physicalCoverageDrawCount, 1);
  assert.equal(initial.antialias, 'disabled-single-sample');
  assert.deepEqual(initial.samplePattern, [[0, 0]]);
  assert.equal(animated.wavePhase, 6);
  assert.deepEqual(camera.pointer, [100, 0]);
  assert.deepEqual(camera.cameraPosition, [95.84220064142759, 0, 1000]);
});

test('formal manifest preserves the one-object ordinary RenderClass policy', async () => {
  const manifest = JSON.parse(await fs.readFile(path.join(
    repositoryRoot,
    'GVMRuntime_ThreeSamples',
    'Manifest',
    'three-r185-manifest.json'), 'utf8'));
  const example = manifest.examples.find(({ id }) => id === 'webgl_points_waves');
  assert.deepEqual(validateManifestContract(example), []);
  const diagnostic = createDiagnosticManifest(manifest, example);
  assert.equal(diagnostic.examples.length, 588);
  assert.equal(
    diagnostic.examples.filter(({ status }) => status === 'phase1_required').length,
    1);
  assert.equal(
    diagnostic.examples.filter(({ status }) => status === 'excluded_upstream').length,
    587);
});

test('camera replay identity remains immutable', async () => {
  const replay = await fs.readFile(path.join(
    scriptDirectory,
    'Inputs',
    'webgl_points_waves_camera.json'));
  assert.equal(
    createHash('sha256').update(replay).digest('hex'),
    '8fe5fa752c1f674042813b6d4e82266a1fd93ec1af207d751b8b4d1f7f1837b4');
});

test('nested field validator accepts float tolerance and reports structural drift', () => {
  const failures = [];
  validateExpectedFields({ nested: { value: 1.5000001 } }, {
    nested: { value: 1.5 }
  }, 'fixture', failures);
  assert.deepEqual(failures, []);
  validateExpectedFields({ nested: [] }, { nested: { value: 1 } }, 'fixture', failures);
  assert.equal(failures.length, 1);
});

test('generated ABI lint accepts ordinary geometry and all Experimental stage products', async () => {
  const temporaryRoot = await fs.mkdtemp(path.join(os.tmpdir(), 'gvm-points-waves-'));
  try {
    const generatedSource = [
      'class WebglPointsWavesMainPass',
      'vertexState.buffers[0].arrayStride = 32',
      'offsetof(WebglPointsWavesVertex, positionAndGridX)',
      'offsetof(WebglPointsWavesVertex, cornerAndGridY)',
      'createBindGroup<WebglPointsWavesResolveResources>',
      'run(WebglPointsWavesIndexCount, 1u, 0u, 0u)',
      'run(3u, 1u, 0u, 0u)',
      ...Array.from({ length: 4 }, (_, sample) => [
        `uniformBuffer${sample}`,
        `WebglPointsWavesSceneSample${sample}`,
        `mainPass${sample}->setVertexBuffer(vertexBuffer)`,
        `mainPass${sample}->setIndexBuffer(indexBuffer)`
      ]).flat()
    ].join('\n');
    for (const pipeline of ['legacy', 'experimental']) {
      const generatedDirectory = path.join(
        temporaryRoot,
        pipeline,
        'WebglPointsWaves',
        'UGLBin');
      await fs.mkdir(generatedDirectory, { recursive: true });
      await Promise.all([
        fs.writeFile(
          path.join(generatedDirectory, 'exports.hpp'),
          'namespace ExportedRenderSet { };\n'),
        fs.writeFile(path.join(generatedDirectory, 'generate_result.hpp'), generatedSource),
        fs.writeFile(
          path.join(generatedDirectory, 'dsl_single_header.hpp'),
          'WebglPointsWavesLogicalPointCount = 2500u;\n'
            + 'WebglPointsWavesVertexCount = WebglPointsWavesLogicalPointCount * 3u;\n')
      ]);
      if (pipeline === 'experimental') {
        for (const passName of ['WebglPointsWavesMainPass']) {
          for (const stage of ['vertex', 'fragment']) {
            const products = [
              path.join('uglir', `${passName}__${stage}.uglir.json`),
              path.join('uglir', `${passName}__${stage}.uglir.txt`),
              path.join('msl', `${passName}__${stage}.msl`),
              path.join('spv', `${passName}__${stage}.raw.spv.txt`)
            ];
            for (const relativePath of products) {
              const productPath = path.join(generatedDirectory, relativePath);
              await fs.mkdir(path.dirname(productPath), { recursive: true });
              await fs.writeFile(productPath, 'locked-product\n');
            }
          }
        }
      }
    }
    const records = await validateGeneratedArtifacts(temporaryRoot);
    assert.deepEqual(records.map(({ pipeline, status, failures }) => ({
      pipeline, status, failures
    })), [
      { pipeline: 'legacy', status: 'pass', failures: [] },
      { pipeline: 'experimental', status: 'pass', failures: [] }
    ]);
  } finally {
    await fs.rm(temporaryRoot, { recursive: true, force: true });
  }
});
