#!/usr/bin/env node

import { spawn } from 'node:child_process';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

import { writeReports } from './report.mjs';
import { runRenderSetPhase0Fixture } from './fixture.mjs';
import { captureThreeReference } from './reference-capture.mjs';
import { assertThreeInputLock } from '../../../../GVMRuntime_ThreeSamples/Tools/asset_oracle_lock.mjs';
import {
  expandSelector,
  loadJson,
  makeRunTimestamp,
  normalizeStatus,
  requiredBackends,
  requiredPipelines,
  runThreeMatrix,
  selectCases,
  writeJson
} from './runner.mjs';

const scriptPath = fileURLToPath(import.meta.url);
const scriptDir = path.dirname(scriptPath);
const sourceDir = path.resolve(scriptDir, '..', '..', '..', '..');
const defaultManifestPath = path.join(sourceDir, 'GVMRuntime_ThreeSamples', 'Manifest', 'three-r185-manifest.json');

/** Parses the runner command and long-form command-line options. */
function parseArguments(argv) {
  const [, , command = 'help', ...tokens] = argv;
  const options = {};
  for (let index = 0; index < tokens.length; index += 1) {
    const token = tokens[index];
    if (!token.startsWith('--')) {
      throw new Error(`Unexpected positional argument '${token}'.`);
    }
    const key = token.slice(2);
    const next = tokens[index + 1];
    if (next && !next.startsWith('--')) {
      options[key] = next;
      index += 1;
    } else {
      options[key] = 'true';
    }
  }
  return { command, options };
}

/** Parses a strict positive integer command-line option. */
function parsePositiveInteger(value, fallback, label) {
  if (value == null) {
    return fallback;
  }
  const parsed = Number(value);
  if (!Number.isInteger(parsed) || parsed < 1) {
    throw new Error(`${label} must be a positive integer; received '${value}'.`);
  }
  return parsed;
}

/** Parses a required non-negative integer command-line option. */
function parseNonNegativeInteger(value, label) {
  const parsed = Number(value);
  if (!Number.isInteger(parsed) || parsed < 0) {
    throw new Error(`${label} must be a non-negative integer; received '${value}'.`);
  }
  return parsed;
}

/** Parses a required unsigned 32-bit integer command-line option. */
function parseUint32(value, label) {
  const parsed = parseNonNegativeInteger(value, label);
  if (parsed > 0xffffffff) {
    throw new Error(`${label} must not exceed 4294967295; received '${value}'.`);
  }
  return parsed;
}

/** Parses a conventional boolean command-line option. */
function parseBoolean(value, fallback) {
  if (value == null) {
    return fallback;
  }
  const normalized = String(value).trim().toLowerCase();
  if (['1', 'true', 'yes', 'on'].includes(normalized)) {
    return true;
  }
  if (['0', 'false', 'no', 'off'].includes(normalized)) {
    return false;
  }
  throw new Error(`Invalid boolean value '${value}'.`);
}

/** Creates the one supported Three r185 runner profile with optional explicit path overrides. */
function createProfile(profileName, options) {
  if (profileName !== 'macos-three-r185') {
    throw new Error(`Unknown profile '${profileName}'. Expected macos-three-r185.`);
  }
  const buildDir = path.resolve(options['build-dir'] ?? path.join(sourceDir, 'build', 'three-r185-dual'));
  const legacyOverride = options['legacy-host'] ? path.resolve(options['legacy-host']) : '';
  const experimentalOverride = options['experimental-host'] ? path.resolve(options['experimental-host']) : '';
  return {
    name: profileName,
    buildDir,
    hosts: {
      legacy: legacyOverride,
      experimental: experimentalOverride
    },
    hostCandidates: {
      legacy: [
        path.join(buildDir, 'gvm_three_samples', 'bin', 'gvm_three_sample_host_legacy'),
        path.join(buildDir, 'bin', 'gvm_three_sample_host_legacy'),
        path.join(buildDir, 'GVMRuntime_ThreeSamples', 'gvm_three_sample_host_legacy')
      ],
      experimental: [
        path.join(buildDir, 'gvm_three_samples', 'bin', 'gvm_three_sample_host_experimental'),
        path.join(buildDir, 'bin', 'gvm_three_sample_host_experimental'),
        path.join(buildDir, 'GVMRuntime_ThreeSamples', 'gvm_three_sample_host_experimental')
      ]
    }
  };
}

/** Calculates and prints immutable manifest accounting and deferred reason totals. */
function printManifestSummary(manifest) {
  const statusCounts = new Map();
  const deferredReasons = new Map();
  for (const example of manifest.examples ?? []) {
    statusCounts.set(example.status, (statusCounts.get(example.status) ?? 0) + 1);
    if (example.status === 'deferred_missing_capability') {
      const reason = example.deferredEvidence?.reasonCode ?? '<missing-reason>';
      deferredReasons.set(reason, (deferredReasons.get(reason) ?? 0) + 1);
    }
  }
  const excluded = statusCounts.get('excluded_upstream') ?? 0;
  const deferred = statusCounts.get('deferred_missing_capability') ?? 0;
  const required = statusCounts.get('phase1_required') ?? 0;
  const pending = statusCounts.get('audit_pending') ?? 0;
  console.log(`Three.js ${manifest.upstream?.release ?? 'r185'} manifest: ${(manifest.examples ?? []).length} examples`);
  console.log(`588 = ${excluded} excluded_upstream + ${deferred} deferred_missing_capability + ${required} phase1_required + ${pending} audit_pending`);
  console.log(`Required gate denominator P: ${required}`);
  console.log(`Real coverage denominator: 511`);
  if (deferredReasons.size > 0) {
    console.log('Deferred capability debt:');
    for (const [reason, count] of [...deferredReasons.entries()].sort()) {
      console.log(`  ${reason}: ${count}`);
    }
  }
}

/** Runs the reproducibility, schema, and RenderSet policy checks as isolated Node processes. */
async function executeManifestLint(manifestPath, strict, upstreamRoot) {
  const toolDir = path.join(sourceDir, 'GVMRuntime_ThreeSamples', 'Tools');
  const manifestDir = path.dirname(manifestPath);
  const commands = [
    [
      path.join(toolDir, 'audit_three_capabilities.mjs'),
      '--check',
      '--upstream-root', upstreamRoot,
      '--manifest', manifestPath,
      '--output', path.join(manifestDir, 'three-r185-capability-audit.json')
    ],
    [path.join(toolDir, 'generate_capability_adjudication.mjs'), '--check'],
    [path.join(toolDir, 'validate_capability_adjudication.mjs'), '--upstream-root', upstreamRoot],
    [path.join(toolDir, 'lock_phase1_manifest.mjs'), '--upstream-root', upstreamRoot, '--manifest-output', manifestPath, '--check'],
    [path.join(toolDir, 'generate_manifest.mjs'), '--check', '--output', manifestPath],
    [
      path.join(toolDir, 'validate_manifest_schema.mjs'),
      '--manifest', manifestPath,
      '--schema', path.join(manifestDir, 'three-r185-manifest.schema.json'),
      '--format', 'text'
    ],
    [path.join(toolDir, 'validate_manifest.mjs'), '--manifest', manifestPath],
    [path.join(toolDir, 'lint_render_set_policy.mjs'), '--manifest', manifestPath, ...(strict ? ['--strict'] : [])],
    [path.join(toolDir, 'lint_sample_gpu_boundary.mjs'), '--source-root', sourceDir, '--format', 'text']
  ];
  for (const [toolPath] of commands) {
    try {
      await fs.access(toolPath);
    } catch {
      throw new Error(`Manifest tool is missing: ${toolPath}.`);
    }
  }
  for (const args of commands) {
    const exitCode = await new Promise((resolve, reject) => {
      const child = spawn(process.execPath, args, { cwd: sourceDir, shell: false, stdio: 'inherit', env: process.env });
      child.on('error', reject);
      child.on('close', resolve);
    });
    if (exitCode !== 0) {
      throw new Error(`${path.basename(args[0])} failed with exit code ${exitCode}.`);
    }
  }
}

/** Runs direct Legacy/Experimental generated-output ABI lint for manifest cases or one explicit fixture. */
async function executeGeneratedArtifactLint(profile, manifestPath, fixture = '') {
  const toolPath = path.join(sourceDir, 'GVMRuntime_ThreeSamples', 'Tools', 'lint_generated_artifacts.mjs');
  const args = [
    toolPath,
    '--legacy-root', path.join(profile.buildDir, 'generated', 'three-r185', 'legacy'),
    '--experimental-root', path.join(profile.buildDir, 'generated', 'three-r185', 'uglir')
  ];
  if (fixture) {
    args.push('--fixture', fixture);
  } else {
    args.push('--manifest', manifestPath);
  }
  const exitCode = await new Promise((resolve, reject) => {
    const child = spawn(process.execPath, args, { cwd: sourceDir, shell: false, stdio: 'inherit', env: process.env });
    child.on('error', reject);
    child.on('close', resolve);
  });
  if (exitCode !== 0) {
    throw new Error(`lint_generated_artifacts.mjs failed with exit code ${exitCode}.`);
  }
}

/** Executes the full selected Three r185 four-quadrant gate. */
async function executeRun(options) {
  const profileName = options.profile ?? 'macos-three-r185';
  const profile = createProfile(profileName, options);
  const manifestPath = path.resolve(options.manifest ?? defaultManifestPath);
  const manifest = await loadJson(manifestPath);
  const status = normalizeStatus(options.status ?? 'phase1-required');
  const group = options.group ?? 'full';
  selectCases(manifest, status, group);
  const assetPackOption = options['asset-pack-root'] ?? options['asset-root'];
  if (options['asset-pack-root'] && options['asset-root']
    && path.resolve(options['asset-pack-root']) !== path.resolve(options['asset-root'])) {
    throw new Error('--asset-pack-root and compatibility alias --asset-root must resolve to the same path when both are supplied.');
  }
  if (!options['upstream-root'] || !assetPackOption || !options['oracle-root'] || !options['input-lock']) {
    throw new Error('run requires explicit --upstream-root, --asset-pack-root, --oracle-root, and --input-lock paths.');
  }
  const upstreamRoot = path.resolve(options['upstream-root']);
  const assetRoot = path.resolve(assetPackOption);
  const oracleRoot = path.resolve(options['oracle-root']);
  await executeManifestLint(manifestPath, true, upstreamRoot);
  const inputPreflight = await assertThreeInputLock({
    manifestPath,
    manifest,
    upstreamRoot,
    assetPackRoot: assetRoot,
    oracleRoot,
    externalAssetMapPath: options['external-asset-map']
      ? path.resolve(options['external-asset-map'])
      : '',
    lockPath: path.resolve(options['input-lock'])
  });
  console.log(`Input preflight passed: files=${inputPreflight.fileCount} required_scenarios=${inputPreflight.requiredScenarios} lock_sha256=${inputPreflight.lockSha256}`);
  await executeGeneratedArtifactLint(profile, manifestPath);
  const pipelines = expandSelector(options.pipeline, requiredPipelines, 'pipeline');
  const backends = expandSelector(options.backend, requiredBackends, 'backend');
  const runRoot = path.resolve(options['report-root'] ?? path.join(profile.buildDir, 'three-r185-reports'));
  const runDir = path.join(runRoot, makeRunTimestamp());
  const report = await runThreeMatrix({
    sourceDir,
    runDir,
    profile,
    manifest,
    assetRoot,
    oracleRoot,
    status,
    group,
    pipelines,
    backends,
    repetitions: parsePositiveInteger(options.repeat, 3, '--repeat'),
    timeoutMs: parsePositiveInteger(options['timeout-ms'], 30_000, '--timeout-ms'),
    onProgress: (result, completed) => {
      const prefix = result.status === 'pass' ? 'PASS' : 'FAIL';
      console.log(`[${prefix}] ${completed} ${result.caseId}/${result.scenarioId} ${result.pipeline}/${result.backend} repeat=${result.repetition}`);
      for (const failure of result.failures) {
        console.log(`  ${failure}`);
      }
    }
  });
  report.profile = profile.name;
  report.manifestPath = manifestPath;
  report.inputPreflight = inputPreflight;
  report.startedFrom = sourceDir;
  report.runDir = runDir;
  const reportPaths = await writeReports(runDir, report);
  if (report.coverage.matrixComplete && report.coverage.selectionComplete) {
    console.log(`Required gate: ${report.coverage.passingRequired}/${report.coverage.requiredTotal} (${(report.coverage.gatePassRate * 100).toFixed(2)}%)`);
  } else {
    console.log('Required gate: not evaluated because the run is not the full required selection and Legacy+Experimental x Metal+Vulkan matrix.');
  }
  console.log(`Real coverage: ${report.coverage.passingRequired}/511 (${(report.coverage.realCoverageRate * 100).toFixed(2)}%)`);
  console.log(`Report: ${reportPaths.htmlPath}`);
  if (report.status === 'diagnostic-pass') {
    console.log('Diagnostic run passed, but it is not a complete P/P gate.');
  }
  if (report.status === 'fail') {
    process.exitCode = 1;
  }
}

/** Executes the RenderSet Phase 0 generated lint and four-quadrant runtime gate. */
async function executeFixture(options) {
  const profileName = options.profile ?? 'macos-three-r185';
  const profile = createProfile(profileName, options);
  const timeoutMs = parsePositiveInteger(options['timeout-ms'], 30_000, '--timeout-ms');
  const reportRoot = path.resolve(options['report-root'] ?? path.join(profile.buildDir, 'three-r185-fixture-reports'));
  const explicitReportPath = options['report-json'] ? path.resolve(options['report-json']) : null;
  const runDir = explicitReportPath === null
    ? path.join(reportRoot, makeRunTimestamp())
    : path.dirname(explicitReportPath);
  const reportPath = explicitReportPath ?? path.join(runDir, 'summary.json');
  const startedAt = new Date().toISOString();

  try {
    await executeGeneratedArtifactLint(profile, defaultManifestPath, 'RenderSetPhase0ScenePass:instancing');
  } catch (error) {
    const failure = error instanceof Error ? error.message : String(error);
    const report = {
      schemaVersion: 1,
      gate: 'render-set-phase0-four-quadrant',
      status: 'fail',
      startedAt,
      profile: profile.name,
      buildDir: profile.buildDir,
      generatedArtifactLint: { status: 'fail', failures: [failure] },
      quadrants: [],
      crossComparisons: []
    };
    await writeJson(reportPath, report);
    console.error(`RenderSet Phase 0 generated-artifact lint failed: ${failure}`);
    console.log(`Fixture JSON: ${reportPath}`);
    process.exitCode = 1;
    return;
  }

  const report = await runRenderSetPhase0Fixture({
    sourceDir,
    runDir,
    buildDir: profile.buildDir,
    hosts: profile.hosts,
    timeoutMs,
    onProgress: (result, completed) => {
      const prefix = result.status === 'pass' ? 'PASS' : 'FAIL';
      console.log(`[${prefix}] ${completed}/4 RenderSetPhase0 ${result.pipeline}/${result.backend}`);
      for (const failure of result.failures) {
        console.log(`  ${failure}`);
      }
    }
  });
  report.startedAt = startedAt;
  report.profile = profile.name;
  report.buildDir = profile.buildDir;
  report.runDir = runDir;
  report.generatedArtifactLint = { status: 'pass', failures: [] };
  await writeJson(reportPath, report);
  for (const comparison of report.crossComparisons) {
    const prefix = comparison.status === 'pass' ? 'PASS' : 'FAIL';
    console.log(`[${prefix}] ${comparison.relation}`);
    for (const failure of comparison.failures) {
      console.log(`  ${failure}`);
    }
  }
  console.log(`Fixture JSON: ${reportPath}`);
  if (report.status !== 'pass') {
    process.exitCode = 1;
  }
}

/** Captures one pinned Three r185 browser frame into the canonical Oracle pack. */
async function executeReferenceCapture(options) {
  const requiredOptions = [
    'chrome', 'upstream-root', 'asset-pack-root', 'oracle-root',
    'case-id', 'scenario-id', 'frame', 'random-seed'
  ];
  for (const optionName of requiredOptions) {
    if (options[optionName] == null || options[optionName] === '') {
      throw new Error(`capture-reference requires --${optionName}.`);
    }
  }
  const result = await captureThreeReference({
    chromePath: path.resolve(options.chrome),
    upstreamRoot: path.resolve(options['upstream-root']),
    assetPackRoot: path.resolve(options['asset-pack-root']),
    oracleRoot: path.resolve(options['oracle-root']),
    externalAssetMapPath: options['external-asset-map']
      ? path.resolve(options['external-asset-map'])
      : '',
    caseId: options['case-id'],
    scenarioId: options['scenario-id'],
    frame: parseNonNegativeInteger(options.frame, '--frame'),
    randomSeed: parseUint32(options['random-seed'], '--random-seed'),
    inputReplayPath: options['input-replay'] ? path.resolve(options['input-replay']) : '',
    stateScriptPath: options['state-script'] ? path.resolve(options['state-script']) : '',
    moduleStateScriptPath: options['module-state-script']
      ? path.resolve(options['module-state-script'])
      : '',
    sampleMode: options['sample-mode'] ?? 'single',
    captureMode: options['capture-mode'] ?? 'renderer-surfaces',
    timeoutMs: parsePositiveInteger(options['timeout-ms'], 30_000, '--timeout-ms')
  });
  console.log(`Reference captured: ${result.pageTitle}`);
  console.log(`RGBA8 Oracle: ${result.rgbaPath}`);
  console.log(`Oracle metadata: ${result.metadataPath}`);
}

/** Prints concise command help for the Three r185 gate. */
function printHelp() {
  console.log(`Usage:
  node tests/runners/three/node/cli.mjs lint --upstream-root <three-r185-checkout> [--strict true|false]
  node tests/runners/three/node/cli.mjs lint-generated [--build-dir <path>] [--fixture <Pass[:instancing]>]
  node tests/runners/three/node/cli.mjs fixture [--build-dir <path>] [--legacy-host <path>] [--experimental-host <path>]
  node tests/runners/three/node/cli.mjs capture-reference --chrome <path> --upstream-root <three-r185-checkout> --asset-pack-root <asset-pack> --oracle-root <oracle-pack> --case-id <id> --scenario-id <id> --frame <index> --random-seed <uint32> [--sample-mode single] [--capture-mode renderer-surfaces|page-composite] [--input-replay <path>] [--external-asset-map <map.json>]
  node tests/runners/three/node/cli.mjs summary
  node tests/runners/three/node/cli.mjs run --profile macos-three-r185 --status phase1-required --pipeline all --backend all --group full --upstream-root <three-r185-checkout> --asset-pack-root <asset-pack> --oracle-root <oracle-pack> --input-lock <lock.json> [--external-asset-map <map.json>]

Run options:
  --build-dir <path>            Dual-pipeline CMake build directory
  --legacy-host <path>          Explicit Legacy host executable
  --experimental-host <path>    Explicit Experimental host executable
  --pipeline <name|all>         legacy, experimental, or all
  --backend <name|all>          metal, vulkan, or all
  --group <full|shard-N-of-M>   Deterministic manifest selection
  --repeat <count>              Repeat each deterministic capture (default 3; gate requires stability)
  --timeout-ms <milliseconds>   Per-capture watchdog (default 30000)
  --upstream-root <path>        Exact local Three r185 source checkout used by the lock
  --asset-pack-root <path>      Immutable local asset pack rooted at the examples URL namespace
  --asset-root <path>           Compatibility alias for --asset-pack-root
  --oracle-root <path>          Immutable 800x500 RGBA8 Oracle pack
  --input-lock <path>           Deterministic source, asset, and Oracle lock JSON
  --external-asset-map <path>   Offline exact-URL and URL-prefix asset map JSON
  --chrome <path>               Explicit Chrome executable for reference capture
  --case-id <id>                Exact Three example identifier for reference capture
  --scenario-id <id>            Canonical scenario identifier for reference capture
  --frame <index>               Virtual 60 Hz frame index for reference capture
  --random-seed <uint32>        Shared GVM/Three xorshift32 seed
  --input-replay <path>         Optional deterministic target-relative pointer replay JSON
  --state-script <path>         Optional global canonical-state JavaScript evaluated before frames
  --module-state-script <path>  Optional canonical-state JavaScript injected into the example module
  --capture-mode <mode>         Renderer-surface capture, or explicit DOM page composite
  --report-root <path>          Report output root
  --report-json <path>          Exact machine-readable fixture report path
  --no-open-report <boolean>    Accepted for compatibility; reports are never opened automatically`);
}

/** Dispatches the Three runner command. */
async function main() {
  const { command, options } = parseArguments(process.argv);
  const manifestPath = path.resolve(options.manifest ?? defaultManifestPath);
  if (command === 'lint') {
    if (!options['upstream-root']) {
      throw new Error('lint requires explicit --upstream-root.');
    }
    await executeManifestLint(
      manifestPath,
      parseBoolean(options.strict, false),
      path.resolve(options['upstream-root'])
    );
    return;
  }
  if (command === 'summary') {
    printManifestSummary(await loadJson(manifestPath));
    return;
  }
  if (command === 'lint-generated') {
    const profile = createProfile(options.profile ?? 'macos-three-r185', options);
    await executeGeneratedArtifactLint(profile, manifestPath, options.fixture ?? '');
    return;
  }
  if (command === 'run') {
    parseBoolean(options['no-open-report'], true);
    await executeRun(options);
    return;
  }
  if (command === 'fixture') {
    await executeFixture(options);
    return;
  }
  if (command === 'capture-reference') {
    await executeReferenceCapture(options);
    return;
  }
  printHelp();
  if (command !== 'help' && command !== '--help' && command !== '-h') {
    process.exitCode = 2;
  }
}

main().catch((error) => {
  console.error(error instanceof Error ? error.stack ?? error.message : String(error));
  process.exitCode = 1;
});
