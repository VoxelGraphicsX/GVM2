import test from 'node:test';
import assert from 'node:assert/strict';

import {
  structuralCases,
  validateSnapshot
} from './validate_phase1_structural_fixture.mjs';

test('structural fixture declares exactly the 20 active cases', () => {
  assert.equal(Object.keys(structuralCases).length, 20);
});

test('valid non-instanced snapshot satisfies the single-sample contract', () => {
  const errors = validateSnapshot({
    caseId: 'webgl_geometry_shapes',
    renderSetPolicy: 'required',
    gpuWorkDslOnly: true,
    sceneRenderSetCount: 1,
    renderSetType: 'WebglGeometryShapesSceneRenderSet',
    renderableObjectCount: 93,
    entityCount: 93,
    instanceCount: 1,
    instanceCounts: Array(93).fill(1),
    scenePassCount: 1,
    screenPassCount: 0,
    directDrawFallback: false,
    sampleCount: 1,
    msaaEnabled: false,
    assetAndAlgorithmState: 'structural-fixture-only'
  });
  assert.deepEqual(errors, []);
});

test('invalid instance and MSAA metadata are rejected', () => {
  const errors = validateSnapshot({
    caseId: 'webgpu_fog_height',
    renderSetPolicy: 'required',
    gpuWorkDslOnly: true,
    sceneRenderSetCount: 1,
    renderSetType: 'WebgpuFogHeightSceneRenderSet',
    renderableObjectCount: 1,
    entityCount: 1,
    instanceCount: 1,
    instanceCounts: [1],
    scenePassCount: 1,
    screenPassCount: 1,
    directDrawFallback: false,
    sampleCount: 4,
    msaaEnabled: true,
    assetAndAlgorithmState: 'structural-fixture-only'
  });
  assert.match(errors.join('\n'), /instanceCount expected 100/);
  assert.match(errors.join('\n'), /sampleCount expected 1/);
  assert.match(errors.join('\n'), /msaaEnabled expected false/);
});
