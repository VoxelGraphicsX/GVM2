import assert from 'node:assert/strict';
import { promises as fs } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import {
  decodeDiscPng,
  generateTextureAsset,
  parseArguments
} from './generate_webgl_points_billboards_texture.mjs';

const repositoryRoot = path.resolve(import.meta.dirname, '../../..');
const pinnedDiscPath = path.join(
  repositoryRoot,
  'build',
  'three-r185-status-validation',
  'examples',
  'textures',
  'sprites',
  'disc.png'
);

test('point texture generator rejects malformed option pairs and duplicates', () => {
  assert.throws(() => parseArguments(['node', 'tool', '--output']), /Expected --option value pair/u);
  assert.throws(
    () => parseArguments(['node', 'tool', '--output', 'a', '--output', 'b']),
    /Duplicate --output/u
  );
});

test('point texture decoder emits the exact vertically flipped RGBA8 base level', async () => {
  const png = await fs.readFile(pinnedDiscPath);
  const decoded = decodeDiscPng(png);
  assert.equal(decoded.length, 32 * 32 * 4);
  assert.deepEqual([...decoded.subarray(0, 16)], [
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0
  ]);
  assert.deepEqual([...decoded.subarray((16 * 32 + 16) * 4, (16 * 32 + 16) * 4 + 4)], [255, 255, 255, 255]);
});

test('point texture generator writes the locked versioned envelope', async () => {
  const temporaryRoot = await fs.mkdtemp(path.join(os.tmpdir(), 'gvm-three-points-billboards-'));
  const assetRoot = path.join(temporaryRoot, 'assets');
  const sourcePath = path.join(assetRoot, 'textures', 'sprites', 'disc.png');
  const outputPath = path.join(temporaryRoot, 'generated', 'disc.bin');
  await fs.mkdir(path.dirname(sourcePath), { recursive: true });
  await fs.copyFile(pinnedDiscPath, sourcePath);
  const result = await generateTextureAsset(assetRoot, outputPath);
  const output = await fs.readFile(outputPath);
  assert.deepEqual(result, { outputPath, extent: 32, byteCount: 4112 });
  assert.equal(output.subarray(0, 8).toString('ascii'), 'DISC185\0');
  assert.equal(output.readUInt32LE(8), 1);
  assert.equal(output.readUInt32LE(12), 32);
  assert.equal(output.length, 4112);
});
