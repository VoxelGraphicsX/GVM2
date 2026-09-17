import assert from 'node:assert/strict';
import { promises as fs } from 'node:fs';
import test from 'node:test';

import {
  defaultExclusionsPath,
  defaultManifestPath
} from './manifest_common.mjs';
import { validateManifest } from './validate_manifest.mjs';

/** Loads an independent copy of the checked-in manifest and exclusion source. */
async function loadValidationFixture() {
  const [manifestText, exclusionsText] = await Promise.all([
    fs.readFile(defaultManifestPath, 'utf8'),
    fs.readFile(defaultExclusionsPath, 'utf8')
  ]);
  return {
    manifest: JSON.parse(manifestText),
    exclusions: JSON.parse(exclusionsText)
  };
}

test('Loader and Exporter required cases must declare semantic gate scenarios', async () => {
  const { manifest, exclusions } = await loadValidationFixture();
  const loader = manifest.examples.find((example) => example.id === 'webgl_geometry_cube');
  loader.id = 'webgl_loader_fixture';
  loader.upstreamPath = 'examples/webgl_loader_fixture.html';
  let validation = validateManifest(manifest, exclusions);
  assert.ok(validation.failures.some((failure) => (
    failure.includes('webgl_loader_fixture') && failure.includes('loader-snapshot')
  )));

  loader.scenarios.push({
    id: 'canonical-loader',
    kind: 'loader-snapshot',
    frame: 0,
    inputReplay: null,
    canonicalState: 'canonical-loaded-scene'
  });
  loader.id = 'misc_exporter_fixture';
  loader.upstreamPath = 'examples/misc_exporter_fixture.html';
  validation = validateManifest(manifest, exclusions);
  assert.ok(validation.failures.some((failure) => (
    failure.includes('misc_exporter_fixture') && failure.includes('export-round-trip')
  )));

  loader.scenarios.push({
    id: 'canonical-round-trip',
    kind: 'export-round-trip',
    frame: 1,
    inputReplay: null,
    canonicalState: 'canonical-export-reimport'
  });
  validation = validateManifest(manifest, exclusions);
  assert.ok(!validation.failures.some((failure) => (
    failure.includes('misc_exporter_fixture') && failure.includes('export-round-trip')
  )));
});

test('scenario object counts and pass invocations must match the locked Scene contract', async () => {
  const { manifest, exclusions } = await loadValidationFixture();
  const example = manifest.examples.find((candidate) => candidate.id === 'webgl_materials_channels');
  const scenario = example.scenarios.find((candidate) => candidate.id === 'velocity-ortho-back');
  scenario.scenePassInvocations.push({
    sceneRoot: 'scene',
    scenePass: 'unknown-pass',
    invocationCount: 1
  });
  scenario.renderableObjectCount = example.renderableObjectCount - 1;
  const validation = validateManifest(manifest, exclusions);
  assert.ok(validation.failures.some((failure) => failure.includes("unknown Scene pass 'scene::unknown-pass'")));
  assert.ok(validation.failures.some((failure) => failure.includes('changes renderableObjectCount without dynamic objects')));
});

test('ordered Scene pass sequences must reference the locked passes and match invocation counts', async () => {
  const { manifest, exclusions } = await loadValidationFixture();
  const example = manifest.examples.find((candidate) => candidate.id === 'webgl_materials_channels');
  const scenario = example.scenarios.find((candidate) => candidate.id === 'velocity-ortho-back');
  scenario.scenePassSequence = [{
    sceneRoot: 'scene',
    scenePass: 'back-facing',
    entityOrdinal: 0
  }];
  let validation = validateManifest(manifest, exclusions);
  assert.ok(!validation.failures.some((failure) => failure.includes(`Scenario '${scenario.id}' sequence`)));

  scenario.scenePassSequence[0].scenePass = 'unknown-pass';
  validation = validateManifest(manifest, exclusions);
  assert.ok(validation.failures.some((failure) => failure.includes("sequence references unknown Scene pass 'scene::unknown-pass'")));
  assert.ok(validation.failures.some((failure) => failure.includes("sequence contains 0 invocation(s) of 'scene::back-facing', expected 1")));
});

test('locked Scene pass and generated RenderClass identities must be unambiguous', async () => {
  const { manifest, exclusions } = await loadValidationFixture();
  const channels = manifest.examples.find((candidate) => candidate.id === 'webgl_materials_channels');
  channels.scenePasses.push(structuredClone(channels.scenePasses[0]));
  const cube = manifest.examples.find((candidate) => candidate.id === 'webgl_geometry_cube');
  cube.scenePasses[0].renderClass = channels.scenePasses[0].renderClass;
  const validation = validateManifest(manifest, exclusions);
  assert.ok(validation.failures.some((failure) => failure.includes("Duplicate Scene pass 'scene::front-facing'")));
  assert.ok(validation.failures.some((failure) => failure.includes('is already owned by webgl_geometry_cube')));
});

test('manifest locks all upstream MSAA requests to ordinary single-sample rendering', async () => {
  const { manifest, exclusions } = await loadValidationFixture();
  manifest.samplePolicy.simulateMsaa = true;
  let validation = validateManifest(manifest, exclusions);
  assert.ok(validation.failures.some((failure) => failure.includes(
    'samplePolicy must keep MSAA examples in scope'
  )));

  manifest.samplePolicy.simulateMsaa = false;
  const example = manifest.examples.find((candidate) => candidate.id === 'webgl_geometry_cube');
  example.screenPasses.push('deterministic-supersample-resolve');
  validation = validateManifest(manifest, exclusions);
  assert.ok(validation.failures.some((failure) => failure.includes(
    "Single-sample policy forbids MSAA or supersampling declaration 'deterministic-supersample-resolve'"
  )));

  example.screenPasses = ['four-sample-resolve'];
  validation = validateManifest(manifest, exclusions);
  assert.ok(validation.failures.some((failure) => failure.includes(
    "Single-sample policy forbids MSAA or supersampling declaration 'four-sample-resolve'"
  )));
});
