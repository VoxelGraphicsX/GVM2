import { spawn } from 'node:child_process';
import { promises as fs } from 'node:fs';
import path from 'node:path';

import {
  loadJson,
  requiredBackends,
  requiredPipelines,
  runThreeMatrix
} from '../../../tests/runners/three/node/runner.mjs';
import { writeReports } from '../../../tests/runners/three/node/report.mjs';

const sourceRoot = path.resolve(import.meta.dirname, '../../..');
const caseIds = Object.freeze([
  'webgl_reversed_depth_buffer',
  'webgpu_reversed_depth_buffer'
]);
const lockedUpstreamCommit = '2431a09f46f34c560bc8e44b33be0e567723d5b9';

/** Parses explicit paths required by the reversed-depth strict gate. */
function parseOptions(argumentsList) {
  const values = new Map();
  for (let index = 0; index < argumentsList.length; index += 2) {
    const key = argumentsList[index];
    const value = argumentsList[index + 1];
    if (!key?.startsWith('--') || value == null) {
      throw new Error(`Invalid option sequence near '${key ?? '<end>'}'.`);
    }
    values.set(key.slice(2), value);
  }
  for (const key of ['build-dir', 'asset-root', 'oracle-root', 'report-root']) {
    if (!values.has(key)) throw new Error(`Missing required --${key}.`);
  }
  return Object.fromEntries(
    [...values].map(([key, value]) => [key, path.resolve(value)]));
}

/** Runs one repository lint command and rejects any non-zero exit code. */
async function executeNodeTool(argumentsList) {
  await new Promise((resolve, reject) => {
    const child = spawn(process.execPath, argumentsList, {
      cwd: sourceRoot,
      env: process.env,
      shell: false,
      stdio: 'inherit'
    });
    child.on('error', reject);
    child.on('close', (exitCode) => {
      if (exitCode === 0) resolve();
      else reject(new Error(
        `${path.basename(argumentsList[0])} exited with code ${exitCode}.`));
    });
  });
}

/** Verifies the immutable r185 reference metadata for both examples. */
async function verifyLockedOracles(oracleRoot) {
  for (const caseId of caseIds) {
    for (const scenarioId of ['initial', 'animated']) {
      const metadata = await loadJson(
        path.join(oracleRoot, caseId, `${scenarioId}.json`));
      const rgbaPath = path.join(
        oracleRoot,
        caseId,
        `${scenarioId}.rgba`);
      const rgbaBytes = await fs.stat(rgbaPath);
      if (metadata.upstreamCommit !== lockedUpstreamCommit
        || metadata.caseId !== caseId
        || metadata.scenarioId !== scenarioId
        || metadata.width !== 800
        || metadata.height !== 500
        || rgbaBytes.size !== 800 * 500 * 4) {
        throw new Error(
          `${caseId}/${scenarioId}: Oracle is not the locked r185 800x500 capture.`);
      }
    }
  }
}

/** Executes generated ABI checks for each shard and the GPU-boundary lint. */
async function runStaticLints(buildDir, manifestPath) {
  for (const caseId of caseIds) {
    await executeNodeTool([
      path.join(
        sourceRoot,
        'GVMRuntime_ThreeSamples',
        'Tools',
        'lint_generated_artifacts.mjs'),
      '--legacy-root', path.join(
        buildDir,
        'generated',
        'three-r185',
        'legacy'),
      '--experimental-root', path.join(
        buildDir,
        'generated',
        'three-r185',
        'experimental'),
      '--manifest', manifestPath,
      '--case', caseId
    ]);
  }
  await executeNodeTool([
    path.join(
      sourceRoot,
      'GVMRuntime_ThreeSamples',
      'Tools',
      'lint_sample_gpu_boundary.mjs'),
    '--source-root', sourceRoot,
    '--format', 'text'
  ]);
}

/** Runs both examples through four quadrants, three repetitions, and cross checks. */
async function main() {
  const options = parseOptions(process.argv.slice(2));
  const manifestPath = path.join(
    sourceRoot,
    'GVMRuntime_ThreeSamples',
    'Manifest',
    'three-r185-manifest.json');
  const fullManifest = await loadJson(manifestPath);
  for (const caseId of caseIds) {
    if (!fullManifest.examples.some((candidate) => candidate.id === caseId)) {
      throw new Error(`${caseId}: missing from the frozen r185 manifest.`);
    }
  }
  const gateManifest = {
    ...fullManifest,
    examples: fullManifest.examples.map((candidate) => (
      caseIds.includes(candidate.id)
        ? candidate
        : { ...candidate, status: 'excluded_upstream' }
    ))
  };

  await verifyLockedOracles(options['oracle-root']);
  await runStaticLints(options['build-dir'], manifestPath);

  const report = await runThreeMatrix({
    sourceDir: sourceRoot,
    runDir: options['report-root'],
    profile: {
      name: 'macos-three-r185-reversed-depth-strict',
      buildDir: options['build-dir'],
      hosts: { legacy: '', experimental: '' },
      hostCandidates: { legacy: [], experimental: [] }
    },
    manifest: gateManifest,
    assetRoot: options['asset-root'],
    oracleRoot: options['oracle-root'],
    status: 'phase1_required',
    group: 'full',
    pipelines: requiredPipelines,
    backends: requiredBackends,
    repetitions: 3,
    timeoutMs: 30_000,
    onProgress: (result, completed) => {
      console.log(
        `[${result.status.toUpperCase()}] ${completed}/48 `
        + `${result.caseId}/${result.scenarioId} `
        + `${result.pipeline}/${result.backend} repeat=${result.repetition}`);
      for (const failure of result.failures) console.log(`  ${failure}`);
    }
  });
  report.schemaVersion = 1;
  report.gate = 'three-r185-reversed-depth-pair-four-quadrant-three-repeat';
  report.caseIds = [...caseIds];
  report.staticLints = {
    generatedArtifacts: 'pass',
    gpuBoundary: 'pass'
  };
  report.authoritativeManifestAccounting = {
    excluded: fullManifest.examples.filter(
      (candidate) => candidate.status === 'excluded_upstream').length,
    deferred: fullManifest.examples.filter(
      (candidate) => candidate.status ===
        'deferred_missing_capability').length,
    required: fullManifest.examples.filter(
      (candidate) => candidate.status === 'phase1_required').length
  };
  const paths = await writeReports(options['report-root'], report);
  console.log(`Strict summary: ${paths.jsonPath}`);
  if (report.status !== 'pass') process.exitCode = 1;
}

await main();
