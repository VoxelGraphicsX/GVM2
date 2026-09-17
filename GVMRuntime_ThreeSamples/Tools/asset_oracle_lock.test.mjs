import assert from 'node:assert/strict';
import { promises as fs } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import {
  generateThreeInputLock,
  serializeThreeInputLock,
  sha256Bytes,
  validateThreeInputLock,
  writeThreeInputLock
} from './asset_oracle_lock.mjs';

const ORACLE_BYTE_COUNT = 800 * 500 * 4;

/** Writes one fixture file after creating every required parent directory. */
async function writeFixtureFile(filePath, contents) {
  await fs.mkdir(path.dirname(filePath), { recursive: true });
  await fs.writeFile(filePath, contents);
}

/** Builds a fixed-size 588-case manifest with the required 77 exclusions. */
function createFixtureManifest() {
  const examples = Array.from({ length: 588 }, (_value, index) => ({
    id: index === 0 ? 'fixture_case' : `fixture_case_${index}`,
    upstreamPath: 'examples/fixture_case.html',
    status: index === 0 ? 'phase1_required' : index <= 77 ? 'excluded_upstream' : 'audit_pending',
    scenarios: index === 0 ? [{
      id: 'initial-frame',
      frame: 0,
      inputReplay: 'inputs/replay.json',
      canonicalState: 'states/canonical.json'
    }] : []
  }));
  return {
    schemaVersion: 1,
    upstream: { release: 'r185', commit: '2431a09f46f34c560bc8e44b33be0e567723d5b9' },
    examples
  };
}

/** Creates a complete local source, asset, Oracle, and manifest fixture tree. */
async function createInputFixture() {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'gvm-three-lock-'));
  const upstreamRoot = path.join(root, 'upstream');
  const assetPackRoot = path.join(root, 'assets');
  const oracleRoot = path.join(root, 'oracle');
  const manifestPath = path.join(root, 'manifest.json');
  const lockPath = path.join(root, 'input-lock.json');
  const manifest = createFixtureManifest();
  await writeFixtureFile(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`);
  await writeFixtureFile(path.join(upstreamRoot, 'examples', 'fixture_case.html'), `
    <!-- <img src="textures/comment-only.png"> -->
    <link rel="stylesheet" href="main.css">
    <script type="importmap">{"imports":{"three":"../build/three.module.js","three/addons/":"./jsm/"}}</script>
    <script type="module">
      import * as THREE from 'three';
      import { addon } from 'three/addons/addon.js';
      new THREE.TextureLoader().load('textures/crate.png');
      // new THREE.TextureLoader().load('textures/comment-only.png');
      new Loader().setPath('models/robot/').load('scene.gltf');
      new Decoder().setDecoderPath('decoders/');
    </script>
  `);
  await writeFixtureFile(path.join(upstreamRoot, 'examples', 'main.css'), `
    /* body { background: url('textures/comment-only.png'); } */
    body { background: url('textures/ui.png'); }
  `);
  await writeFixtureFile(path.join(upstreamRoot, 'build', 'three.module.js'), 'export const version = 185;\n');
  await writeFixtureFile(path.join(upstreamRoot, 'examples', 'jsm', 'addon.js'), `
    import './helper.js';
    export const worker = new URL('../workers/parser.js', import.meta.url);
    export const addon = true;
  `);
  await writeFixtureFile(path.join(upstreamRoot, 'examples', 'jsm', 'helper.js'), 'export const helper = true;\n');
  await writeFixtureFile(path.join(upstreamRoot, 'examples', 'workers', 'parser.js'), 'self.onmessage = () => {};\n');
  await writeFixtureFile(path.join(assetPackRoot, 'textures', 'crate.png'), Buffer.from([1, 2, 3]));
  await writeFixtureFile(path.join(assetPackRoot, 'textures', 'ui.png'), Buffer.from([4, 5, 6]));
  await writeFixtureFile(path.join(assetPackRoot, 'models', 'robot', 'scene.gltf'), JSON.stringify({
    asset: { version: '2.0' },
    buffers: [{ uri: 'scene.bin' }],
    images: [{ uri: 'albedo.png' }]
  }));
  await writeFixtureFile(path.join(assetPackRoot, 'models', 'robot', 'scene.bin'), Buffer.from([7, 8, 9]));
  await writeFixtureFile(path.join(assetPackRoot, 'models', 'robot', 'albedo.png'), Buffer.from([10, 11, 12]));
  await writeFixtureFile(path.join(assetPackRoot, 'decoders', 'decoder.wasm'), Buffer.from([13, 14, 15]));
  const replayBytes = Buffer.from(`${JSON.stringify({
    schemaVersion: 1,
    caseId: 'fixture_case',
    scenarioId: 'initial-frame',
    frame: 0,
    target: '#fixture-canvas',
    events: [
      { type: 'pointerdown', x: 1, y: 2 },
      { type: 'pointerup', x: 1, y: 2 }
    ]
  }, null, 2)}\n`);
  await writeFixtureFile(path.join(assetPackRoot, 'inputs', 'replay.json'), replayBytes);
  await writeFixtureFile(path.join(assetPackRoot, 'states', 'canonical.json'), '{"state":"fixed"}\n');
  await writeFixtureFile(path.join(oracleRoot, 'fixture_case', 'initial-frame.rgba'), Buffer.alloc(ORACLE_BYTE_COUNT, 32));
  await writeFixtureFile(path.join(oracleRoot, 'fixture_case', 'initial-frame.json'), JSON.stringify({
    schemaVersion: 1,
    source: 'three-r185-reference',
    upstreamCommit: '2431a09f46f34c560bc8e44b33be0e567723d5b9',
    caseId: 'fixture_case',
    scenarioId: 'initial-frame',
    frame: 0,
    virtualTimeMs: 0,
    nextFrameTimeMs: 1000 / 60,
    width: 800,
    height: 500,
    randomSeed: 0x12345678,
    rowStrideBytes: 3200,
    byteCount: ORACLE_BYTE_COUNT,
    format: 'rgba8unorm',
    canvasBackingWidth: 800,
    canvasBackingHeight: 500,
    samplePolicy: {
      mode: 'single-sample',
      msaaEnabled: false,
      simulateMsaa: false
    },
    inputReplay: {
      caseId: 'fixture_case',
      scenarioId: 'initial-frame',
      captureFrame: 0,
      sha256: sha256Bytes(replayBytes),
      eventCount: 2,
      target: '#fixture-canvas'
    }
  }));
  return { root, upstreamRoot, assetPackRoot, oracleRoot, manifestPath, lockPath, manifest };
}

/** Removes one complete fixture tree regardless of individual test outcome. */
async function removeInputFixture(fixture) {
  await fs.rm(fixture.root, { recursive: true, force: true });
}

test('input lock deterministically captures source, transitive assets, Oracle files, and referring cases', async () => {
  const fixture = await createInputFixture();
  try {
    const options = fixture;
    const first = await generateThreeInputLock(options);
    const second = await generateThreeInputLock(options);
    assert.equal(serializeThreeInputLock(first), serializeThreeInputLock(second));
    assert.equal(first.scope.manifestExamples, 588);
    assert.equal(first.scope.excludedExamples, 77);
    assert.equal(first.scope.scannedExamples, 511);
    assert.equal(first.scope.requiredScenarios, 1);
    assert.ok(first.files.some((entry) => entry.root === 'upstream' && entry.relativePath === 'build/three.module.js'));
    assert.ok(first.files.some((entry) => entry.root === 'upstream' && entry.relativePath === 'examples/workers/parser.js'));
    assert.ok(first.files.some((entry) => entry.root === 'assetPack' && entry.relativePath === 'models/robot/scene.bin'));
    assert.ok(first.files.some((entry) => entry.root === 'assetPack' && entry.relativePath === 'decoders/decoder.wasm'));
    assert.ok(first.files.some((entry) => entry.root === 'assetPack' && entry.relativePath === 'inputs/replay.json'));
    assert.ok(first.files.some((entry) => entry.root === 'assetPack' && entry.relativePath === 'states/canonical.json'));
    assert.ok(first.files.some((entry) => entry.root === 'oracle' && entry.relativePath === 'fixture_case/initial-frame.rgba'));
    assert.ok(first.files.every((entry) => !path.isAbsolute(entry.relativePath) && /^[a-f0-9]{64}$/u.test(entry.sha256)));
    assert.deepEqual(first.externalReferences, []);
    assert.equal(first.externalAssetMap, null);
    await writeThreeInputLock(fixture.lockPath, first);
    const preflight = await validateThreeInputLock({ ...options, lockPath: fixture.lockPath });
    assert.equal(preflight.status, 'pass', preflight.failures?.join('\n'));
  } finally {
    await removeInputFixture(fixture);
  }
});

test('input lock requires and hashes an exact external asset map for required references', async () => {
  const fixture = await createInputFixture();
  try {
    const htmlPath = path.join(fixture.upstreamRoot, 'examples', 'fixture_case.html');
    const html = await fs.readFile(htmlPath, 'utf8');
    const moduleClose = html.lastIndexOf('</script>');
    await fs.writeFile(
      htmlPath,
      `${html.slice(0, moduleClose)}fetch('https://example.invalid/remote.glb');\n${html.slice(moduleClose)}`
    );
    await assert.rejects(
      () => generateThreeInputLock(fixture),
      /need --external-asset-map/u
    );

    const externalBytes = Buffer.from([21, 22, 23, 24]);
    const externalPath = path.join(fixture.assetPackRoot, 'external', 'remote.glb');
    const externalAssetMapPath = path.join(fixture.root, 'external-assets.json');
    await writeFixtureFile(externalPath, externalBytes);
    await writeFixtureFile(externalAssetMapPath, `${JSON.stringify({
      schemaVersion: 1,
      mappings: [{
        type: 'exact',
        url: 'https://example.invalid/remote.glb',
        assetPackPath: 'external/remote.glb',
        sha256: sha256Bytes(externalBytes),
        byteSize: externalBytes.byteLength
      }]
    }, null, 2)}\n`);
    const options = { ...fixture, externalAssetMapPath };
    const lock = await generateThreeInputLock(options);
    const mappedFile = lock.files.find((entry) => (
      entry.root === 'assetPack' && entry.relativePath === 'external/remote.glb'
    ));
    assert.equal(lock.externalAssetMap.sha256, sha256Bytes(await fs.readFile(externalAssetMapPath)));
    assert.equal(lock.externalAssetMap.mappedFileCount, 1);
    assert.equal(lock.externalReferences[0].kind, 'asset');
    assert.equal(lock.externalReferences[0].reference, 'https://example.invalid/remote.glb');
    assert.equal(lock.externalReferences[0].caseIds.length, 511);
    assert.equal(mappedFile.sha256, sha256Bytes(externalBytes));
    assert.equal(mappedFile.caseIds.length, 511);
    await writeThreeInputLock(fixture.lockPath, lock);
    const preflight = await validateThreeInputLock({ ...options, lockPath: fixture.lockPath });
    assert.equal(preflight.status, 'pass', preflight.failures?.join('\n'));

    await fs.appendFile(externalAssetMapPath, '\n');
    const drifted = await validateThreeInputLock({ ...options, lockPath: fixture.lockPath });
    assert.equal(drifted.status, 'fail');
    assert.ok(drifted.failures.some((failure) => failure.includes('External asset map bytes')));
  } finally {
    await removeInputFixture(fixture);
  }
});

test('input preflight rejects byte-size and SHA-256 drift', async () => {
  const fixture = await createInputFixture();
  try {
    const lock = await generateThreeInputLock(fixture);
    await writeThreeInputLock(fixture.lockPath, lock);
    await fs.writeFile(path.join(fixture.assetPackRoot, 'textures', 'crate.png'), Buffer.from([99, 98, 97, 96]));
    const assetPreflight = await validateThreeInputLock({ ...fixture, lockPath: fixture.lockPath });
    assert.equal(assetPreflight.status, 'fail');
    assert.ok(assetPreflight.failures.some((failure) => failure.includes("Hash drift for 'assetPack:textures/crate.png'")));
    await fs.writeFile(path.join(fixture.assetPackRoot, 'textures', 'crate.png'), Buffer.from([1, 2, 3]));
    const oraclePath = path.join(fixture.oracleRoot, 'fixture_case', 'initial-frame.rgba');
    const oracle = await fs.readFile(oraclePath);
    oracle[0] ^= 0xff;
    await fs.writeFile(oraclePath, oracle);
    const oraclePreflight = await validateThreeInputLock({ ...fixture, lockPath: fixture.lockPath });
    assert.equal(oraclePreflight.status, 'fail');
    assert.ok(oraclePreflight.failures.some((failure) => failure.includes("Hash drift for 'oracle:fixture_case/initial-frame.rgba'")));
  } finally {
    await removeInputFixture(fixture);
  }
});

test('lock generation rejects missing and non-800x500 required Oracles', async () => {
  const fixture = await createInputFixture();
  try {
    const metadataPath = path.join(fixture.oracleRoot, 'fixture_case', 'initial-frame.json');
    const validMetadata = JSON.parse(await fs.readFile(metadataPath, 'utf8'));
    await fs.writeFile(metadataPath, JSON.stringify({ ...validMetadata, width: 400, height: 250 }));
    await assert.rejects(() => generateThreeInputLock(fixture), /must declare 800x500/u);
    await fs.writeFile(metadataPath, JSON.stringify({ ...validMetadata, randomSeed: 7 }));
    await assert.rejects(() => generateThreeInputLock(fixture), /randomSeed must be 305419896/u);
    await fs.writeFile(metadataPath, JSON.stringify(validMetadata));
    await fs.rm(path.join(fixture.oracleRoot, 'fixture_case', 'initial-frame.rgba'));
    await assert.rejects(() => generateThreeInputLock(fixture), /missing Oracle RGBA/u);
  } finally {
    await removeInputFixture(fixture);
  }
});

test('input lock validates modern single- and multi-canvas Oracle provenance', async () => {
  const fixture = await createInputFixture();
  try {
    const metadataPath = path.join(fixture.oracleRoot, 'fixture_case', 'initial-frame.json');
    const metadata = JSON.parse(await fs.readFile(metadataPath, 'utf8'));
    metadata.referenceCaptureMode = 'multi-canvas-composite';
    metadata.sourceCanvases = [
      { index: 0, x: 0, y: 0, width: 200, height: 500, backingWidth: 200, backingHeight: 500 },
      { index: 1, x: 200, y: 0, width: 600, height: 500, backingWidth: 600, backingHeight: 500 }
    ];
    await fs.writeFile(metadataPath, JSON.stringify(metadata));
    const multiCanvasLock = await generateThreeInputLock(fixture);
    assert.equal(multiCanvasLock.scope.requiredScenarios, 1);

    metadata.sourceCanvases[1].x = 250;
    metadata.sourceCanvases[1].width = 550;
    await fs.writeFile(metadataPath, JSON.stringify(metadata));
    await assert.rejects(() => generateThreeInputLock(fixture), /Oracle provenance covers only/u);

    metadata.referenceCaptureMode = 'single-canvas';
    metadata.sourceCanvases = [
      { index: 0, x: 0, y: 0, width: 800, height: 500, backingWidth: 800, backingHeight: 500 }
    ];
    await fs.writeFile(metadataPath, JSON.stringify(metadata));
    const singleCanvasLock = await generateThreeInputLock(fixture);
    assert.equal(singleCanvasLock.scope.requiredScenarios, 1);

    metadata.referenceCaptureMode = 'renderer-surface-composite';
    metadata.sourceSurfaces = [
      { index: 0, kind: 'canvas', tagName: 'canvas', x: 0, y: 0, width: 800, height: 500, backingWidth: 800, backingHeight: 500 },
      { index: 1, kind: 'css-renderer', tagName: 'div', x: 0, y: 0, width: 800, height: 500, backingWidth: null, backingHeight: null }
    ];
    delete metadata.sourceCanvases;
    await fs.writeFile(metadataPath, JSON.stringify(metadata));
    const layeredSurfaceLock = await generateThreeInputLock(fixture);
    assert.equal(layeredSurfaceLock.scope.requiredScenarios, 1);

    metadata.referenceCaptureMode = 'page-composite';
    metadata.sourceSurfaces = [
      { index: 0, kind: 'canvas', tagName: 'canvas', x: 8, y: 111, width: 784, height: 784, backingWidth: 1024, backingHeight: 1024 }
    ];
    await fs.writeFile(metadataPath, JSON.stringify(metadata));
    const pageCompositeLock = await generateThreeInputLock(fixture);
    assert.equal(pageCompositeLock.scope.requiredScenarios, 1);
  } finally {
    await removeInputFixture(fixture);
  }
});

test('Loader and Exporter semantic Oracles are validated and hashed by the input lock', async () => {
  const fixture = await createInputFixture();
  try {
    const example = fixture.manifest.examples[0];
    const scenario = example.scenarios[0];
    scenario.kind = 'export-round-trip';
    const semanticHash = 'a'.repeat(64);
    const semanticPath = path.join(
      fixture.oracleRoot,
      'fixture_case',
      'initial-frame.semantic.json'
    );
    await writeFixtureFile(fixture.manifestPath, `${JSON.stringify(fixture.manifest, null, 2)}\n`);
    await writeFixtureFile(semanticPath, `${JSON.stringify({
      schemaVersion: 1,
      caseId: 'fixture_case',
      scenarioId: 'initial-frame',
      frame: 0,
      kind: 'export-round-trip',
      canonicalState: 'states/canonical.json',
      result: {
        canonicalOutputSha256: 'b'.repeat(64),
        sourceSemanticSha256: semanticHash,
        reimportedSemanticSha256: semanticHash,
        roundTripEquivalent: true
      }
    }, null, 2)}\n`);
    const lock = await generateThreeInputLock(fixture);
    assert.ok(lock.files.some((entry) => (
      entry.root === 'oracle'
        && entry.relativePath === 'fixture_case/initial-frame.semantic.json'
    )));
    assert.equal(
      lock.oracleScenarios[0].semanticPath,
      'fixture_case/initial-frame.semantic.json'
    );

    const invalid = JSON.parse(await fs.readFile(semanticPath, 'utf8'));
    invalid.result.reimportedSemanticSha256 = 'c'.repeat(64);
    await fs.writeFile(semanticPath, JSON.stringify(invalid));
    await assert.rejects(
      () => generateThreeInputLock(fixture),
      /does not prove an exact canonical round-trip/u
    );
  } finally {
    await removeInputFixture(fixture);
  }
});

test('input lock accepts deterministic keyboard and wheel replay events', async () => {
  const fixture = await createInputFixture();
  try {
    const replayPath = path.join(fixture.assetPackRoot, 'inputs', 'replay.json');
    const replayBytes = Buffer.from(`${JSON.stringify({
      schemaVersion: 1,
      caseId: 'fixture_case',
      scenarioId: 'initial-frame',
      frame: 0,
      target: 'body',
      events: [
        { type: 'keydown', key: 'w', code: 'KeyW', shiftKey: true },
        { type: 'wheel', x: 400, y: 250, deltaY: -120 },
        { type: 'keyup', key: 'w', code: 'KeyW' }
      ]
    }, null, 2)}\n`);
    await fs.writeFile(replayPath, replayBytes);
    const metadataPath = path.join(fixture.oracleRoot, 'fixture_case', 'initial-frame.json');
    const metadata = JSON.parse(await fs.readFile(metadataPath, 'utf8'));
    metadata.inputReplay.sha256 = sha256Bytes(replayBytes);
    metadata.inputReplay.eventCount = 3;
    metadata.inputReplay.target = 'body';
    await fs.writeFile(metadataPath, JSON.stringify(metadata));
    const lock = await generateThreeInputLock(fixture);
    assert.equal(lock.oracleScenarios[0].inputReplaySha256, sha256Bytes(replayBytes));

    const invalid = JSON.parse(replayBytes.toString('utf8'));
    invalid.events[0].key = '';
    await fs.writeFile(replayPath, JSON.stringify(invalid));
    await assert.rejects(() => generateThreeInputLock(fixture), /keyboard event 0 is invalid/u);
  } finally {
    await removeInputFixture(fixture);
  }
});
