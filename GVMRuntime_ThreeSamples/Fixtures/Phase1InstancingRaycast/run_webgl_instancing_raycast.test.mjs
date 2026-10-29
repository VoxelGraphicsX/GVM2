import assert from 'node:assert/strict';
import { promises as fs } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { buildStabilityComparison } from './run_webgl_instancing_raycast.mjs';

test('an exact pixel mismatch fails the complete stability result even within visual tolerance', async (context) => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'raycast-stability-'));
  context.after(() => fs.rm(directory, { recursive: true, force: true }));
  const pixels = Buffer.alloc(800 * 500 * 4, 255);
  const runs = [];
  for (const repetition of [1, 2, 3]) {
    const rgbaPath = path.join(directory, `${repetition}.rgba`);
    const metadataPath = path.join(directory, `${repetition}.json`);
    await fs.writeFile(rgbaPath, pixels);
    await fs.writeFile(metadataPath, JSON.stringify({ width: 800, height: 500 }));
    runs.push({ scenarioId: 'initial', pipeline: 'experimental', backend: 'metal',
      repetition, status: 'pass', artifacts: { rgbaPath, metadataPath } });
  }
  assert.equal((await buildStabilityComparison(runs)).status, 'pass');
  pixels[0] -= 1;
  await fs.writeFile(runs[2].artifacts.rgbaPath, pixels);
  const result = await buildStabilityComparison(runs);
  assert.equal(result.status, 'fail');
  assert.equal(result.comparisons[1].status, 'fail');
  assert.match(result.failures.join('\n'), /Repeated RGBA pixels differ/u);
});
