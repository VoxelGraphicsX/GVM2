import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { promises as fs } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import {
  createExternalAssetCaptureResponse,
  loadExternalAssetMap,
  makeExternalAssetMapLockRecord,
  normalizeExternalAssetPath,
  resolveExternalAssetRoute,
  validateRequiredExternalAssetCoverage
} from './external_asset_map.mjs';

/** Calculates the lowercase SHA-256 digest for one test payload. */
function sha256(bytes) {
  return createHash('sha256').update(bytes).digest('hex');
}

/** Writes one fixture file after creating every required parent directory. */
async function writeFixtureFile(filePath, contents) {
  await fs.mkdir(path.dirname(filePath), { recursive: true });
  await fs.writeFile(filePath, contents);
}

/** Creates one temporary external-map fixture and registers automatic cleanup. */
async function createMapFixture(context) {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'gvm-external-map-'));
  context.after(() => fs.rm(root, { recursive: true, force: true }));
  const assetPackRoot = path.join(root, 'assets');
  const mapPath = path.join(root, 'external-assets.json');
  await fs.mkdir(assetPackRoot, { recursive: true });
  return { root, assetPackRoot, mapPath };
}

/** Writes a schemaVersion 1 map document to one fixture path. */
async function writeMap(fixture, mappings) {
  await fs.writeFile(fixture.mapPath, `${JSON.stringify({ schemaVersion: 1, mappings }, null, 2)}\n`);
}

test('exact URL mapping verifies bytes and uniquely covers a required module', async (context) => {
  const fixture = await createMapFixture(context);
  const bytes = Buffer.from('export const offline = true;\n');
  await writeFixtureFile(path.join(fixture.assetPackRoot, 'external', 'module.js'), bytes);
  await writeMap(fixture, [{
    type: 'exact',
    url: 'https://cdn.example.test/module.js',
    assetPackPath: 'external/module.js',
    sha256: sha256(bytes),
    byteSize: bytes.byteLength
  }]);

  const assetMap = await loadExternalAssetMap(fixture.mapPath, fixture.assetPackRoot);
  const coverage = validateRequiredExternalAssetCoverage([{
    kind: 'module',
    reference: 'https://cdn.example.test/module.js',
    caseIds: ['required-case']
  }], new Set(['required-case']), assetMap);
  assert.equal(coverage.requiredReferenceCount, 1);
  assert.deepEqual([...coverage.routeCaseIds.get('https://cdn.example.test/module.js')], ['required-case']);
  assert.deepEqual(makeExternalAssetMapLockRecord(assetMap), {
    schemaVersion: 1,
    byteSize: (await fs.stat(fixture.mapPath)).size,
    sha256: sha256(await fs.readFile(fixture.mapPath)),
    mappingCount: 1,
    mappedFileCount: 1
  });
});

test('URL-prefix mapping enumerates directory files and rejects traversal paths', async (context) => {
  const fixture = await createMapFixture(context);
  const javascript = Buffer.from('export const decoder = true;\n');
  const wasm = Buffer.from([0, 97, 115, 109]);
  await writeFixtureFile(path.join(fixture.assetPackRoot, 'external', 'decoder.js'), javascript);
  await writeFixtureFile(path.join(fixture.assetPackRoot, 'external', 'decoder.wasm'), wasm);
  await writeMap(fixture, [{
    type: 'url-prefix',
    urlPrefix: 'https://cdn.example.test/decoder/',
    files: [
      {
        relativePath: 'decoder.js',
        assetPackPath: 'external/decoder.js',
        sha256: sha256(javascript),
        byteSize: javascript.byteLength
      },
      {
        relativePath: 'decoder.wasm',
        assetPackPath: 'external/decoder.wasm',
        sha256: sha256(wasm),
        byteSize: wasm.byteLength
      }
    ]
  }]);

  const assetMap = await loadExternalAssetMap(fixture.mapPath, fixture.assetPackRoot);
  const coverage = validateRequiredExternalAssetCoverage([{
    kind: 'asset-directory',
    reference: 'https://cdn.example.test/decoder/',
    caseIds: ['decoder-case']
  }], ['decoder-case'], assetMap);
  assert.equal(coverage.routeCaseIds.size, 2);
  assert.equal(resolveExternalAssetRoute(assetMap, 'https://cdn.example.test/decoder/decoder.wasm')?.mimeType, 'application/wasm');
  assert.equal(resolveExternalAssetRoute(assetMap, 'https://cdn.example.test/decoder/%2e%2e/secret.bin'), null);
  assert.throws(() => normalizeExternalAssetPath('%2e%2e/secret.bin'), /parent-directory/u);
  assert.throws(() => normalizeExternalAssetPath('/absolute.bin'), /relative/u);
});

test('required external references fail without complete mapping while pending evidence remains allowed', async (context) => {
  const fixture = await createMapFixture(context);
  const bytes = Buffer.from([1, 2, 3]);
  await writeFixtureFile(path.join(fixture.assetPackRoot, 'external', 'other.glb'), bytes);
  await writeMap(fixture, [{
    type: 'exact',
    url: 'https://assets.example.test/other.glb',
    assetPackPath: 'external/other.glb',
    sha256: sha256(bytes),
    byteSize: bytes.byteLength
  }]);
  const references = [{
    kind: 'asset',
    reference: 'https://assets.example.test/required.glb',
    caseIds: ['required-case', 'pending-case']
  }];
  assert.throws(
    () => validateRequiredExternalAssetCoverage(references, ['required-case'], null),
    /need --external-asset-map/u
  );
  assert.doesNotThrow(() => validateRequiredExternalAssetCoverage(references, ['unrelated-case'], null));
  const assetMap = await loadExternalAssetMap(fixture.mapPath, fixture.assetPackRoot);
  assert.throws(
    () => validateRequiredExternalAssetCoverage(references, ['required-case'], assetMap),
    /not enumerated/u
  );
});

test('duplicate exact routes and overlapping URL prefixes are rejected as ambiguous', async (context) => {
  const fixture = await createMapFixture(context);
  const bytes = Buffer.from('x');
  await writeFixtureFile(path.join(fixture.assetPackRoot, 'external', 'x.js'), bytes);
  const descriptor = {
    assetPackPath: 'external/x.js',
    sha256: sha256(bytes),
    byteSize: bytes.byteLength
  };
  await writeMap(fixture, [
    { type: 'exact', url: 'https://cdn.example.test/x.js', ...descriptor },
    { type: 'exact', url: 'https://cdn.example.test/x.js', ...descriptor }
  ]);
  await assert.rejects(
    () => loadExternalAssetMap(fixture.mapPath, fixture.assetPackRoot),
    /covered more than once/u
  );

  await writeMap(fixture, [
    {
      type: 'url-prefix',
      urlPrefix: 'https://cdn.example.test/package/',
      files: [{ relativePath: 'x.js', ...descriptor }]
    },
    {
      type: 'url-prefix',
      urlPrefix: 'https://cdn.example.test/package/nested/',
      files: [{ relativePath: 'x.js', ...descriptor }]
    }
  ]);
  await assert.rejects(
    () => loadExternalAssetMap(fixture.mapPath, fixture.assetPackRoot),
    /overlap ambiguously/u
  );
});

test('schema objects reject unsupported fields instead of silently accepting typos', async (context) => {
  const fixture = await createMapFixture(context);
  const bytes = Buffer.from('x');
  await writeFixtureFile(path.join(fixture.assetPackRoot, 'external', 'x.js'), bytes);
  await writeMap(fixture, [{
    type: 'exact',
    url: 'https://cdn.example.test/x.js',
    assetPackPath: 'external/x.js',
    sha256: sha256(bytes),
    byteSize: bytes.byteLength,
    unexpectedField: true
  }]);
  await assert.rejects(
    () => loadExternalAssetMap(fixture.mapPath, fixture.assetPackRoot),
    /unsupported field\(s\): unexpectedField/u
  );
});

test('declared and post-load mapped-file hash drift both fail closed', async (context) => {
  const fixture = await createMapFixture(context);
  const assetPath = path.join(fixture.assetPackRoot, 'external', 'asset.glb');
  const bytes = Buffer.from([4, 5, 6]);
  await writeFixtureFile(assetPath, bytes);
  const mapping = {
    type: 'exact',
    url: 'https://assets.example.test/asset.glb',
    assetPackPath: 'external/asset.glb',
    sha256: '0'.repeat(64),
    byteSize: bytes.byteLength
  };
  await writeMap(fixture, [mapping]);
  await assert.rejects(
    () => loadExternalAssetMap(fixture.mapPath, fixture.assetPackRoot),
    /hash drift/u
  );

  mapping.sha256 = sha256(bytes);
  await writeMap(fixture, [mapping]);
  const assetMap = await loadExternalAssetMap(fixture.mapPath, fixture.assetPackRoot);
  await fs.writeFile(assetPath, Buffer.from([9, 8, 7, 6]));
  await assert.rejects(
    () => createExternalAssetCaptureResponse(assetMap, mapping.url),
    /hash drift/u
  );
});

test('capture route helper returns local bytes with deterministic MIME and CORS headers', async (context) => {
  const fixture = await createMapFixture(context);
  const bytes = Buffer.from([137, 80, 78, 71]);
  await writeFixtureFile(path.join(fixture.assetPackRoot, 'external', 'pixel.png'), bytes);
  await writeMap(fixture, [{
    type: 'exact',
    url: 'https://images.example.test/pixel.png',
    assetPackPath: 'external/pixel.png',
    sha256: sha256(bytes),
    byteSize: bytes.byteLength
  }]);
  const assetMap = await loadExternalAssetMap(fixture.mapPath, fixture.assetPackRoot);
  const response = await createExternalAssetCaptureResponse(assetMap, 'https://images.example.test/pixel.png');
  assert.equal(response.responseCode, 200);
  assert.deepEqual(Buffer.from(response.body, 'base64'), bytes);
  assert.equal(response.responseHeaders.find((header) => header.name === 'Content-Type')?.value, 'image/png');
  assert.equal(response.responseHeaders.find((header) => header.name === 'Access-Control-Allow-Origin')?.value, '*');
  assert.equal(await createExternalAssetCaptureResponse(assetMap, 'https://images.example.test/unmapped.png'), null);
});
