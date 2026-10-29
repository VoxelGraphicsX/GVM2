import assert from 'node:assert/strict';
import { promises as fs } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import {
  lintManifestSingleSamplePolicy,
  lintSingleSampleSources
} from './lint_single_sample_policy.mjs';

test('single-sample source lint rejects supersample simulation', async () => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'gvm-single-sample-source-'));
  await fs.writeFile(path.join(root, 'Sample.hpp'), 'void deterministicSupersampleResolve();\n');
  assert.equal((await lintSingleSampleSources(root)).length, 1);
  await fs.writeFile(path.join(root, 'Sample.hpp'), 'void renderSingleSample();\n');
  assert.deepEqual(await lintSingleSampleSources(root), []);
});

test('single-sample source lint rejects disguised four-pass averaging', async () => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'three-single-sample-four-pass-'));
  await fs.writeFile(
    path.join(root, 'Sample.hpp'),
    'Texture2D<float4> sceneTexture3; // four-sample rotated-grid resolve\n');
  const result = await lintSingleSampleSources(root);
  assert.equal(result.length, 1);
});

test('single-sample manifest lint requires the complete policy', async () => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'gvm-single-sample-manifest-'));
  const manifestPath = path.join(root, 'manifest.json');
  await fs.writeFile(manifestPath, JSON.stringify({
    samplePolicy: {
      mode: 'single-sample',
      upstreamMsaaDoesNotExcludeExample: true,
      msaaEnabled: false,
      simulateMsaa: false
    }
  }));
  assert.deepEqual(await lintManifestSingleSamplePolicy(manifestPath), []);
});
