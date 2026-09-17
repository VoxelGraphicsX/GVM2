#!/usr/bin/env node
import childProcess from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';

const caseToBinary = new Map([
  ['webgpu_compute_birds', 'Phase1WebgpuComputeRenderSet'],
  ['webgl_postprocessing_fxaa', 'Phase1WebglPostprocessingFxaaRenderSet'],
  ['webgpu_postprocessing_radial_blur', 'Phase1WebgpuPostprocessingRadialBlurRenderSet'],
  ['webgl_shaders_sky', 'Phase1WebglShadersSkyRenderSet'],
  ['webgpu_sky', 'Phase1WebgpuSkyRenderSet'],
]);

const renderSetCases = new Set([
  'webgpu_compute_birds',
  'webgl_postprocessing_fxaa',
  'webgpu_postprocessing_radial_blur',
  'webgl_shaders_sky',
  'webgpu_sky'
]);

/** Parses explicit command-line paths for the non-gating implementation smoke. */
function parseArguments(argv) {
  const output = {};
  for (let index = 2; index < argv.length; index += 2) {
    const name = argv[index];
    const value = argv[index + 1];
    if (!name?.startsWith('--') || value === undefined) throw new Error(`Invalid argument near ${name}.`);
    output[name.slice(2)] = value;
  }
  for (const required of ['binary-root', 'manifest', 'output-dir', 'asset-root']) {
    if (!output[required]) throw new Error(`Missing --${required}.`);
  }
  return output;
}

/** Runs one generated host and returns a compact diagnostic record. */
function executeQuadrant(binaryPath, argumentsList) {
  const execution = childProcess.spawnSync(binaryPath, argumentsList, { encoding: 'utf8' });
  return {
    exitCode: execution.status,
    signal: execution.signal,
    stdout: execution.stdout.trim(),
    stderr: execution.stderr.trim()
  };
}

/** Measures whether one RGBA8 implementation capture contains a meaningful non-flat RGB image. */
function analyzeCapture(rgbaPath) {
  if (!fs.existsSync(rgbaPath)) return { byteCount: 0, meanRgb: 0, dynamicRange: 0 };
  const rgba = fs.readFileSync(rgbaPath);
  let rgbSum = 0;
  let minimum = 255;
  let maximum = 0;
  for (let index = 0; index < rgba.length; index += 4) {
    for (let channel = 0; channel < 3; channel += 1) {
      const value = rgba[index + channel];
      rgbSum += value;
      minimum = Math.min(minimum, value);
      maximum = Math.max(maximum, value);
    }
  }
  return {
    byteCount: rgba.length,
    meanRgb: rgbSum / (rgba.length / 4 * 3),
    dynamicRange: maximum - minimum
  };
}

/** Compares two JSON arrays without accepting implicit coercions. */
function arraysEqual(left, right) {
  return Array.isArray(left) && Array.isArray(right)
    && left.length === right.length
    && left.every((value, index) => value === right[index]);
}

/** Validates the structural contract emitted by one current scaffold host. */
function validateSceneContract(snapshotPath, caseId, scenarioId) {
  if (!fs.existsSync(snapshotPath)) return { passed: false, failures: ['scene snapshot is missing'] };
  const snapshot = JSON.parse(fs.readFileSync(snapshotPath, 'utf8'));
  const failures = [];
  const expectsRenderSet = renderSetCases.has(caseId);
  if (snapshot.caseId !== caseId) failures.push('caseId does not match the execution');
  if (snapshot.scenarioId !== scenarioId) failures.push('scenarioId does not match the execution');
  if (snapshot.implementationLevel !== 'scaffolded') failures.push('implementation level must remain scaffolded');
  if (snapshot.gpuWorkDslOnly !== true) failures.push('GPU work is not declared DSL-only');
  if (snapshot.assetBacked !== false || !arraysEqual(snapshot.assetHashes, [])) {
    failures.push('scaffold must explicitly declare that real assets are not yet locked');
  }
  if (snapshot.directDrawFallback !== false) failures.push('direct draw fallback is forbidden');
  if (expectsRenderSet) {
    if (snapshot.renderSetPolicy !== 'required' || snapshot.sceneRenderSetCount !== 1) {
      failures.push('complex Scene must expose exactly one RenderSet');
    }
    if (!arraysEqual(snapshot.componentSchema,
      ['vertices', 'indices', 'objects', 'instances', 'materials'])) {
      failures.push('RenderSet component schema does not match the scaffold ABI');
    }
    if (snapshot.renderSetIndexedIndirect !== true) failures.push('RenderSet indirect draw is missing');
    if (!arraysEqual(snapshot.computeDispatchThreads, [65536, 1, 1])) {
      failures.push('structured-state compute dispatch must specify total thread count 65536x1x1');
    }
    if (!arraysEqual(snapshot.attachmentFormats, ['rgba16float', 'depth32float', 'rgba8unorm'])) {
      failures.push('complex attachment format contract does not match');
    }
    if (!Array.isArray(snapshot.instanceCounts)
      || snapshot.instanceCounts.length !== snapshot.entityCount
      || snapshot.instanceCounts.some((count) => !Number.isInteger(count) || count < 1)) {
      failures.push('per-entity instance counts are incomplete');
    }
  } else {
    if (snapshot.renderSetPolicy !== 'not-required' || snapshot.sceneRenderSetCount !== 0) {
      failures.push('simple Scene must not expose a RenderSet');
    }
    if (!arraysEqual(snapshot.computeDispatchThreads, [512, 512, 1])) {
      failures.push('texture compute dispatch must specify total thread count 512x512x1');
    }
    if (!arraysEqual(snapshot.attachmentFormats, ['rgba8unorm', 'depth32float'])) {
      failures.push('simple attachment format contract does not match');
    }
    if (!arraysEqual(snapshot.storageTextureFormats, ['rgba8unorm'])) {
      failures.push('simple storage texture format contract does not match');
    }
    if (!(snapshot.vertexCount > 0 && snapshot.indexCount > 0)) {
      failures.push('standalone geometry counts must be positive');
    }
  }
  return { passed: failures.length === 0, failures, snapshot };
}

const options = parseArguments(process.argv);
const manifest = JSON.parse(fs.readFileSync(options.manifest, 'utf8'));
const manifestById = new Map(manifest.examples.map((example) => [example.id, example]));
const executions = [];

for (const [caseId, binaryStem] of caseToBinary) {
  const example = manifestById.get(caseId);
  if (!example) throw new Error(`${caseId} is missing from the manifest.`);
  const scenario = example.scenarios[0];
  for (const pipeline of ['legacy', 'experimental']) {
    for (const backend of ['metal', 'vulkan']) {
      const quadrantDirectory = path.join(options['output-dir'], caseId, `${pipeline}-${backend}`);
      fs.mkdirSync(quadrantDirectory, { recursive: true });
      const binaryPath = path.join(options['binary-root'], `${binaryStem}-${pipeline}`);
      const result = executeQuadrant(binaryPath, [
        '--case-id', caseId,
        '--scenario-id', scenario.id,
        '--pipeline', pipeline,
        '--backend', backend,
        '--random-seed', '305419896',
        '--width', '800',
        '--height', '500',
        '--frame', String(scenario.frame ?? 0),
        '--asset-root', options['asset-root'],
        '--capture-rgba', path.join(quadrantDirectory, 'final.rgba'),
        '--capture-metadata', path.join(quadrantDirectory, 'capture.json'),
        '--scene-snapshot', path.join(quadrantDirectory, 'scene.json'),
        '--canonical-state', example.canonicalState ?? 'seed-42'
      ]);
      const rgbaPath = path.join(quadrantDirectory, 'final.rgba');
      const snapshotPath = path.join(quadrantDirectory, 'scene.json');
      const expectedBytes = 800 * 500 * 4;
      const capture = analyzeCapture(rgbaPath);
      const contract = validateSceneContract(snapshotPath, caseId, scenario.id);
      const passed = result.exitCode === 0 && capture.byteCount === expectedBytes
        && capture.meanRgb >= 1.0 && capture.dynamicRange >= 8 && contract.passed;
      executions.push({
        caseId,
        pipeline,
        backend,
        passed,
        sceneContract: { passed: contract.passed, failures: contract.failures },
        ...capture,
        ...result
      });
      if (!passed) {
        throw new Error(`${caseId} ${pipeline}/${backend} failed: ${JSON.stringify({
          result,
          capture,
          sceneContract: contract
        })}`);
      }
    }
  }
}

const summary = {
  schemaVersion: 1,
  gateKind: 'implementation-smoke',
  strictGateEvidence: false,
  nonBlackValidation: 'meanRgb>=1.0 && dynamicRange>=8',
  sceneContractValidation: true,
  caseCount: caseToBinary.size,
  quadrantExecutionCount: executions.length,
  passed: executions.every((execution) => execution.passed),
  executions
};
fs.mkdirSync(options['output-dir'], { recursive: true });
fs.writeFileSync(path.join(options['output-dir'], 'summary.json'), `${JSON.stringify(summary, null, 2)}\n`);
process.stdout.write(`Phase 1 batch smoke passed: ${summary.caseCount} cases, ${summary.quadrantExecutionCount} executions.\n`);
