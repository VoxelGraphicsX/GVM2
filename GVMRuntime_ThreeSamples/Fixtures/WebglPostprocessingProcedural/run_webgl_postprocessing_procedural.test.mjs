import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import test from 'node:test';

import {
  buildHostArguments,
  parseArguments,
  parseRepeatCount,
  proceduralScenarios,
  validateCanonicalReplayBytes,
  validateExpectedFields
} from './run_webgl_postprocessing_procedural.mjs';

test('parseArguments accepts explicit fixture paths and rejects duplicates', () => {
  const options = parseArguments([
    'node', 'fixture.mjs',
    '--binary-root', '/binaries',
    '--generated-root', '/generated',
    '--output-dir', '/output',
    '--oracle-root', '/oracles',
    '--input-replay-1d', '/inputs/one.json',
    '--input-replay-2d', '/inputs/two.json'
  ]);
  assert.equal(options['binary-root'], '/binaries');
  assert.equal(options['input-replay-2d'], '/inputs/two.json');
  assert.throws(() => parseArguments([
    'node', 'fixture.mjs', '--output-dir', '/one', '--output-dir', '/two'
  ]), /Duplicate --output-dir/u);
});

test('parseRepeatCount defaults to three and rejects weaker stability runs', () => {
  assert.equal(parseRepeatCount(undefined), 3);
  assert.equal(parseRepeatCount('4'), 4);
  assert.throws(() => parseRepeatCount('2'), /at least 3/u);
  assert.throws(() => parseRepeatCount('1.5'), /positive integer/u);
});

test('buildHostArguments passes canonical replay only to replay scenarios', () => {
  const context = {
    inputReplay1dPath: '/inputs/noise-1d.json',
    inputReplay2dPath: '/inputs/noise-2d.json'
  };
  const artifacts = {
    rgbaPath: '/output/capture.rgba',
    metadataPath: '/output/capture.json',
    snapshotPath: '/output/snapshot.json'
  };
  const initialArguments = buildHostArguments(
    context, proceduralScenarios[0], 'legacy', 'metal', artifacts);
  assert.equal(initialArguments.includes('--input-replay'), false);

  const oneDimensionalArguments = buildHostArguments(
    context, proceduralScenarios[1], 'experimental', 'vulkan', artifacts);
  const replayOffset = oneDimensionalArguments.indexOf('--input-replay');
  assert.notEqual(replayOffset, -1);
  assert.equal(oneDimensionalArguments[replayOffset + 1], '/inputs/noise-1d.json');
  assert.equal(
    oneDimensionalArguments[oneDimensionalArguments.indexOf('--scenario-id') + 1],
    'noise-1d');
});

test('validateCanonicalReplayBytes locks SHA and metadata identity', () => {
  const bytes = Buffer.from(`${JSON.stringify({
    schemaVersion: 1,
    caseId: 'webgl_postprocessing_procedural',
    scenarioId: 'noise-1d',
    frame: 1,
    target: '.lil-gui select',
    canonicalState: { procedure: 'noiseRandom1D' },
    events: [{ type: 'keydown', frame: 0, key: '1', code: 'Digit1' }]
  })}\n`);
  const sha256 = createHash('sha256').update(bytes).digest('hex');
  const identity = validateCanonicalReplayBytes(bytes, {
    sha256,
    scenarioId: 'noise-1d',
    frame: 1,
    eventCount: 1,
    target: '.lil-gui select',
    noiseMode: 'noiseRandom1D'
  });
  assert.deepEqual(identity, {
    schemaVersion: 1,
    sha256,
    caseId: 'webgl_postprocessing_procedural',
    scenarioId: 'noise-1d',
    captureFrame: 1,
    eventCount: 1,
    target: '.lil-gui select'
  });
  assert.throws(() => validateCanonicalReplayBytes(bytes, {
    sha256: '0'.repeat(64),
    scenarioId: 'noise-1d',
    frame: 1,
    eventCount: 1,
    target: '.lil-gui select',
    noiseMode: 'noiseRandom1D'
  }), /replay\.noise-1d\.sha256/u);
});

test('validateExpectedFields reports nested replay identity drift', () => {
  const failures = [];
  validateExpectedFields(
    { inputReplay: { sha256: 'actual', eventCount: 2 } },
    { inputReplay: { sha256: 'expected', eventCount: 2 } },
    'metadata',
    failures);
  assert.deepEqual(failures, [
    'metadata.inputReplay.sha256="actual", expected "expected".'
  ]);
});
