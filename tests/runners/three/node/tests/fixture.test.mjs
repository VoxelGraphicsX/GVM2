import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import {
  buildFixtureHostArguments,
  makeFixtureArtifactPaths,
  runFixtureQuadrant,
  runRenderSetPhase0Fixture,
  validateFixtureImage,
  validateFixtureSnapshot
} from '../fixture.mjs';

/** Writes an executable fake Phase 0 host with deterministic CLI-driven artifacts. */
function writeFakeHost(directory, name, mode, geometryRgb = [220, 70, 30]) {
  const hostPath = path.join(directory, name);
  const source = `#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';

const mode = ${JSON.stringify(mode)};
const geometryRgb = ${JSON.stringify(geometryRgb)};
const values = new Map();
for (let index = 2; index < process.argv.length; index += 2) {
  values.set(process.argv[index], process.argv[index + 1]);
}
if (mode === 'timeout') {
  setTimeout(() => {}, 10_000);
} else {
  const width = Number(values.get('--width'));
  const height = Number(values.get('--height'));
  const pixels = Buffer.alloc(width * height * 4);
  for (let pixel = 0; pixel < width * height; pixel += 1) {
    pixels[pixel * 4] = 4;
    pixels[pixel * 4 + 1] = 5;
    pixels[pixel * 4 + 2] = 8;
    pixels[pixel * 4 + 3] = 255;
  }
  if (mode !== 'clear') {
    const palette = [
      geometryRgb,
      [(geometryRgb[0] + 31) % 256, (geometryRgb[1] + 47) % 256, (geometryRgb[2] + 59) % 256],
      [(geometryRgb[0] + 73) % 256, (geometryRgb[1] + 89) % 256, (geometryRgb[2] + 101) % 256],
      [(geometryRgb[0] + 127) % 256, (geometryRgb[1] + 139) % 256, (geometryRgb[2] + 151) % 256]
    ];
    for (let y = 150; y < 350; y += 1) {
      for (let x = 200; x < 600; x += 1) {
        const offset = (y * width + x) * 4;
        const color = palette[(x >= 400 ? 1 : 0) + (y >= 250 ? 2 : 0)];
        pixels[offset] = color[0];
        pixels[offset + 1] = color[1];
        pixels[offset + 2] = color[2];
      }
    }
  }
  const rgbaPath = values.get('--capture-rgba');
  const metadataPath = values.get('--capture-metadata');
  const snapshotPath = values.get('--scene-snapshot');
  for (const outputPath of [rgbaPath, metadataPath, snapshotPath]) {
    fs.mkdirSync(path.dirname(outputPath), { recursive: true });
  }
  fs.writeFileSync(rgbaPath, pixels);
  fs.writeFileSync(metadataPath, JSON.stringify({
    caseId: values.get('--case-id'),
    scenarioId: values.get('--scenario-id'),
    pipeline: values.get('--pipeline'),
    backend: values.get('--backend'),
    frame: Number(values.get('--frame')),
    randomSeed: Number(values.get('--random-seed')),
    width,
    height,
    rowStrideBytes: width * 4,
    byteCount: pixels.length,
    format: 'rgba8unorm'
  }));
  const snapshot = {
    sceneRenderSetCount: 1,
    entityCount: mode === 'bad-snapshot' ? 3 : 2,
    drawCommandCount: 1,
    removedEntity: 7,
    replacementEntity: 7,
    reusedEntityIndex: true,
    mutationApplied: true,
    entities: [
      { role: 'instanced', entity: 8, instanceCount: 3 },
      { role: 'replacement', entity: 7, instanceCount: 1 }
    ],
    componentSchema: ['vertices', 'indices', 'objects', 'instances', 'materials', 'albedo']
  };
  fs.writeFileSync(snapshotPath, JSON.stringify(snapshot));
}
`;
  fs.writeFileSync(hostPath, source, 'utf8');
  fs.chmodSync(hostPath, 0o755);
  return hostPath;
}

/** Creates a temporary fixture execution context with explicit host paths. */
function createContext(rootDir, legacyHost, experimentalHost = legacyHost, timeoutMs = 10_000) {
  return {
    sourceDir: rootDir,
    runDir: path.join(rootDir, 'run'),
    buildDir: path.join(rootDir, 'unused-build'),
    hosts: {
      legacy: legacyHost,
      experimental: experimentalHost
    },
    timeoutMs
  };
}

test('fixture host arguments pin lifecycle frame, resolution, and every capture path', () => {
  const paths = makeFixtureArtifactPaths('/run', 'legacy', 'metal');
  const args = buildFixtureHostArguments('legacy', 'metal', paths);
  assert.deepEqual(args.slice(0, 16), [
    '--case-id', 'render-set-phase0',
    '--scenario-id', 'entity-lifecycle',
    '--frame', '1',
    '--random-seed', '305419896',
    '--pipeline', 'legacy',
    '--backend', 'metal',
    '--width', '800',
    '--height', '500'
  ]);
  assert.ok(args.includes('--capture-rgba'));
  assert.ok(args.includes('--capture-metadata'));
  assert.ok(args.includes('--scene-snapshot'));
});

test('fixture watchdog terminates a non-responsive fake host', async (t) => {
  const rootDir = fs.mkdtempSync(path.join(os.tmpdir(), 'gvm-three-fixture-'));
  t.after(() => fs.rmSync(rootDir, { recursive: true, force: true }));
  const host = writeFakeHost(rootDir, 'timeout-host.mjs', 'timeout');
  const result = await runFixtureQuadrant(createContext(rootDir, host, host, 80), 'legacy', 'metal');
  assert.equal(result.status, 'fail');
  assert.equal(result.host.timedOut, true);
  assert.ok(result.failures.some((failure) => failure.includes('watchdog')));
});

test('fixture rejects a clear-only fake host capture', async (t) => {
  const rootDir = fs.mkdtempSync(path.join(os.tmpdir(), 'gvm-three-fixture-'));
  t.after(() => fs.rmSync(rootDir, { recursive: true, force: true }));
  const host = writeFakeHost(rootDir, 'clear-host.mjs', 'clear');
  const result = await runFixtureQuadrant(createContext(rootDir, host), 'legacy', 'metal');
  assert.equal(result.status, 'fail');
  assert.equal(result.validation.imageValidation.nonUniformRgbPixels, 0);
  assert.ok(result.failures.some((failure) => failure.includes('clear-only')));
});

test('fixture rejects the reserved shader bounds-safety failure color', () => {
  const pixels = new Uint8Array(800 * 500 * 4);
  pixels.fill(255);
  pixels[1] = 0;
  const result = validateFixtureImage({ width: 800, height: 500, pixels });
  assert.ok(result.failures.some((failure) => failure.includes('out-of-bounds component safety probe')));
  assert.equal(result.boundsSafetyFailurePixels, 1);
});

test('fixture snapshot gate detects entity count and removed-entity leakage', () => {
  const failures = validateFixtureSnapshot({
    sceneRenderSetCount: 1,
    entityCount: 3,
    drawCommandCount: 1,
    removedEntity: 4,
    replacementEntity: 9,
    reusedEntityIndex: false,
    mutationApplied: true,
    entities: [
      { role: 'instanced', entity: 8, instanceCount: 3 },
      { role: 'replacement', entity: 9, instanceCount: 1 },
      { role: 'removed', entity: 4, instanceCount: 1 }
    ],
    componentSchema: ['vertices', 'indices', 'objects', 'instances', 'materials', 'albedo']
  });
  assert.ok(failures.some((failure) => failure.includes('entityCount must be 2')));
  assert.ok(failures.some((failure) => failure.includes('exactly 2 live records')));
  assert.ok(failures.some((failure) => failure.includes('removed entity leaked')));
  assert.ok(failures.some((failure) => failure.includes('old entity role leaked')));
});

test('fixture reports cross-pipeline image mismatch without weakening thresholds', async (t) => {
  const rootDir = fs.mkdtempSync(path.join(os.tmpdir(), 'gvm-three-fixture-'));
  t.after(() => fs.rmSync(rootDir, { recursive: true, force: true }));
  const legacyHost = writeFakeHost(rootDir, 'legacy-host.mjs', 'valid', [230, 30, 30]);
  const experimentalHost = writeFakeHost(rootDir, 'experimental-host.mjs', 'valid', [30, 30, 230]);
  const report = await runRenderSetPhase0Fixture(createContext(rootDir, legacyHost, experimentalHost));
  assert.equal(report.status, 'fail');
  assert.equal(report.quadrants.filter((quadrant) => quadrant.status === 'pass').length, 4);
  assert.equal(report.crossComparisons.length, 4);
  assert.equal(report.crossComparisons.find((entry) => entry.relation === 'backend-parity-legacy').status, 'pass');
  assert.equal(report.crossComparisons.find((entry) => entry.relation === 'backend-parity-experimental').status, 'pass');
  assert.equal(report.crossComparisons.find((entry) => entry.relation === 'pipeline-parity-metal').status, 'fail');
  assert.equal(report.thresholds.meanAbsoluteRgb, 2.0);
  assert.equal(report.thresholds.p99AbsoluteRgb, 16);
  assert.equal(report.thresholds.luminanceSsim, 0.995);
});
