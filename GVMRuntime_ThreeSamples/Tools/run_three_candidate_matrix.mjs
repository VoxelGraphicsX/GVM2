#!/usr/bin/env node

import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

import {
  loadJson,
  runThreeMatrix,
  writeJson
} from '../../tests/runners/three/node/runner.mjs';
import { writeReports } from '../../tests/runners/three/node/report.mjs';
import { lintGeneratedArtifacts } from './lint_generated_artifacts.mjs';
import { lintSampleGpuBoundary } from './lint_sample_gpu_boundary.mjs';

const repositoryRoot = path.resolve(new URL('../..', import.meta.url).pathname);
const defaultManifest = path.join(repositoryRoot, 'GVMRuntime_ThreeSamples', 'Manifest', 'three-r185-manifest.json');

/** Parses the small, explicit command-line contract for one candidate case. */
function parseArguments(argv) {
  const options = {};
  for (let index = 2; index < argv.length; index += 1) {
    const token = argv[index];
    if (!token.startsWith('--') || index + 1 >= argv.length) {
      throw new Error(`Expected --name value, received '${token ?? ''}'.`);
    }
    options[token.slice(2)] = argv[index + 1];
    index += 1;
  }
  return options;
}

/** Requires one non-empty path option and resolves it against the repository. */
function requiredPath(options, name) {
  const value = options[name];
  if (!value) throw new Error(`Missing --${name}.`);
  return path.resolve(repositoryRoot, value);
}

/**
 * Selects the built sample executable for one pipeline from an ordered alias
 * list.  The first existing file wins; retaining the manifest shard as the
 * final alias keeps the historical failure mode explicit when no executable
 * was generated.
 */
export async function selectThreeHostExecutable(
  buildDir,
  example,
  pipeline,
  implementationAliases = []) {
  const binRoot = path.join(buildDir, 'gvm_three_samples', 'bin');
  const aliases = [];
  for (const alias of implementationAliases) {
    if (typeof alias === 'string' && alias.length > 0 && !aliases.includes(alias)) {
      aliases.push(alias);
    }
  }
  if (typeof example?.dslShard === 'string' && example.dslShard.length > 0
    && !aliases.includes(example.dslShard)) {
    aliases.push(example.dslShard);
  }
  for (const alias of aliases) {
    const candidate = path.join(binRoot, `${alias}-${pipeline}`);
    try {
      await fs.access(candidate);
      return candidate;
    } catch {
      // Try the next deterministic alias.
    }
  }
  return path.join(binRoot, `${example.dslShard}-${pipeline}`);
}

/**
 * Reads the local implementation inventories and returns Host aliases for a
 * case.  Inventory ownership is preferred over a historical planning shard;
 * ambiguous or malformed entries are ignored so the runner remains usable
 * for cases that have not been migrated.
 */
/** Loads all deterministic Host aliases declared by the implementation inventory. */
export async function loadThreeImplementationAliases(caseId) {
  const dslRoot = path.join(repositoryRoot, 'GVMRuntime_ThreeSamples', 'Dsl');
  let entries;
  try {
    entries = await fs.readdir(dslRoot, { withFileTypes: true });
  } catch {
    return [];
  }
  const aliases = [];
  for (const entry of entries) {
    if (!entry.isDirectory()) continue;
    const shardDirectory = path.join(dslRoot, entry.name);
    const inventoryPath = path.join(shardDirectory, 'implemented-cases.json');
    let inventory;
    try {
      inventory = JSON.parse(await fs.readFile(inventoryPath, 'utf8'));
    } catch {
      continue;
    }
    const implementation = Array.isArray(inventory.cases)
      ? inventory.cases.find((item) => item?.caseId === caseId)
      : null;
    if (!implementation) continue;
    for (const alias of [
      implementation.hostTarget,
      implementation.dslEntry,
      inventory.shard,
      entry.name
    ]) {
      if (typeof alias === 'string' && alias.length > 0 && !aliases.includes(alias)) {
        aliases.push(alias);
      }
    }
  }
  return aliases;
}

/**
 * Resolves a case seed from its locked Oracle metadata when the source
 * manifest intentionally leaves the seed at the generic host default.
 *
 * A case may contain several scenarios, so the override is accepted only
 * when every selected Oracle records the same unsigned 32-bit seed.  Mixed
 * seeds remain an explicit manifest concern and are never guessed here.
 */
export async function resolveThreeCaseRandomSeed(example, oracleRoot) {
  const scenarioSeeds = [];
  for (const scenario of example?.scenarios ?? []) {
    if (typeof scenario?.id !== 'string' || scenario.id.length === 0) continue;
    const metadataPath = path.join(oracleRoot, example.id, `${scenario.id}.json`);
    let metadata;
    try {
      metadata = JSON.parse(await fs.readFile(metadataPath, 'utf8'));
    } catch {
      continue;
    }
    if (Number.isInteger(metadata.randomSeed)
        && metadata.randomSeed >= 0
        && metadata.randomSeed <= 0xffffffff) {
      scenarioSeeds.push(metadata.randomSeed >>> 0);
    }
  }
  if (scenarioSeeds.length === 0) return null;
  const seed = scenarioSeeds[0];
  return scenarioSeeds.every((candidate) => candidate === seed) ? seed : null;
}

/**
 * Copies deterministic input replays referenced by selected examples into the
 * explicit offline asset root used by the Three runner.  Replays are sample
 * inputs, not GPU assets, but the Host contract intentionally resolves both
 * through the same asset-root namespace.
 */
export async function stageThreeScenarioInputs(examples, assetRoot) {
  const root = path.resolve(assetRoot);
  const inputPaths = new Set();
  for (const example of examples ?? []) {
    for (const scenario of example?.scenarios ?? []) {
      if (typeof scenario?.inputReplay === 'string'
        && scenario.inputReplay.length > 0) {
        inputPaths.add(scenario.inputReplay);
      }
    }
  }
  const staged = [];
  for (const relativePath of inputPaths) {
    if (path.isAbsolute(relativePath)
      || relativePath.split(/[\\/]+/u).includes('..')) {
      throw new Error(`Input replay must be a relative asset path: ${relativePath}`);
    }
    const destination = path.join(root, relativePath);
    try {
      await fs.access(destination);
      staged.push({ relativePath, destination, source: 'asset-root-existing' });
      continue;
    } catch {
      // Resolve the immutable repository copy below.
    }
    const basename = path.basename(relativePath);
    const explicitCandidates = [
      path.join(repositoryRoot, 'GVMRuntime_ThreeSamples', relativePath),
      path.join(repositoryRoot, 'GVMRuntime_ThreeSamples', 'inputs', basename),
      // ReferenceStates is the immutable, repository-owned source for the
      // captured input replays used by the Three r185 oracle.  Keep this
      // lookup here rather than copying files into the build tree manually so
      // every candidate and batch run stages the same deterministic bytes.
      path.join(
        repositoryRoot,
        'GVMRuntime_ThreeSamples',
        'ReferenceStates',
        relativePath),
      path.join(
        repositoryRoot,
        'GVMRuntime_ThreeSamples',
        'ReferenceStates',
        'inputs',
        basename)
    ];
    const fixtureRoot = path.join(repositoryRoot, 'GVMRuntime_ThreeSamples', 'Fixtures');
    let fixtureCandidates = [];
    try {
      const fixtureEntries = await fs.readdir(fixtureRoot, { withFileTypes: true });
      fixtureCandidates = fixtureEntries
        .filter((entry) => entry.isDirectory())
        .map((entry) => path.join(fixtureRoot, entry.name, 'Inputs', basename));
    } catch {
      fixtureCandidates = [];
    }
    const candidates = [...explicitCandidates, ...fixtureCandidates];
    const matches = [];
    for (const candidate of candidates) {
      try {
        await fs.access(candidate);
        matches.push(candidate);
      } catch {
        // Continue through the deterministic candidate list.
      }
    }
    const uniqueMatches = [...new Set(matches)];
    if (uniqueMatches.length === 0) {
      // Keep the capture in the matrix.  The Host will emit the concrete
      // missing-replay failure instead of the staging helper aborting an
      // otherwise useful multi-case batch.
      staged.push({ relativePath, destination, source: 'missing' });
      continue;
    }
    if (uniqueMatches.length > 1) {
      throw new Error(`Input replay '${relativePath}' is ambiguous: ${uniqueMatches.join(', ')}`);
    }
    await fs.mkdir(path.dirname(destination), { recursive: true });
    await fs.copyFile(uniqueMatches[0], destination);
    staged.push({
      relativePath,
      destination,
      source: path.relative(repositoryRoot, uniqueMatches[0])
    });
  }
  return staged;
}

/** Runs one case without changing the source Manifest or its audit accounting. */
async function main() {
  const options = parseArguments(process.argv);
  const caseId = options['case-id'];
  if (!caseId) throw new Error('Missing --case-id.');
  const manifestPath = requiredPath(options, 'manifest');
  const sourceManifest = await loadJson(manifestPath);
  if (!Array.isArray(sourceManifest.examples) || sourceManifest.examples.length !== 588) {
    throw new Error('The source Three r185 Manifest must contain exactly 588 examples.');
  }
  const example = sourceManifest.examples.find((entry) => entry.id === caseId);
  if (!example) throw new Error(`Unknown Three r185 example '${caseId}'.`);
  const randomSeed = options['random-seed'] == null
    ? undefined
    : Number(options['random-seed']);
  if (randomSeed !== undefined
    && (!Number.isInteger(randomSeed) || randomSeed < 0 || randomSeed > 0xffffffff)) {
    throw new Error(`Invalid --random-seed '${options['random-seed']}'.`);
  }
  let selectedExample = randomSeed === undefined
    ? example
    : { ...example, randomSeed };
  const scenarioId = options['scenario-id'];
  if (scenarioId) {
    const selectedScenarios = selectedExample.scenarios.filter(
      (scenario) => scenario.id === scenarioId
    );
    if (selectedScenarios.length !== 1) {
      throw new Error(`Unknown scenario '${scenarioId}' for '${caseId}'.`);
    }
    selectedExample = { ...selectedExample, scenarios: selectedScenarios };
  }
  const pipelines = options.pipeline == null || options.pipeline === 'all'
    ? ['legacy', 'experimental']
    : [options.pipeline];
  const backends = options.backend == null || options.backend === 'all'
    ? ['metal', 'vulkan']
    : [options.backend];
  if (pipelines.some((pipeline) => !['legacy', 'experimental'].includes(pipeline))) {
    throw new Error(`Invalid --pipeline '${options.pipeline}'.`);
  }
  if (backends.some((backend) => !['metal', 'vulkan'].includes(backend))) {
    throw new Error(`Invalid --backend '${options.backend}'.`);
  }
  const buildDir = options['build-dir'] ? requiredPath(options, 'build-dir') : null;
  const oracleRoot = requiredPath(options, 'oracle-root');
  if (randomSeed === undefined && !Number.isInteger(selectedExample.randomSeed)) {
    const lockedSeed = await resolveThreeCaseRandomSeed(selectedExample, oracleRoot);
    if (lockedSeed !== null) selectedExample = { ...selectedExample, randomSeed: lockedSeed };
  }
  // Resolve the seed before constructing the private Manifest view.  The
  // runner uses that view when building the command line; creating it first
  // silently fell back to 0x12345678 for cases whose locked Oracle uses a
  // case-specific stream, while the Host still emitted the pinned seed.
  const manifest = {
    ...sourceManifest,
    examples: sourceManifest.examples.map((entry) => (
      entry.id === caseId
        ? selectedExample
        : { ...entry, status: 'excluded_upstream' }
    ))
  };
  const implementationAliases = await loadThreeImplementationAliases(caseId);
  const legacyHost = options['legacy-host']
    ? requiredPath(options, 'legacy-host')
    : await selectThreeHostExecutable(
      buildDir, example, 'legacy', implementationAliases);
  const experimentalHost = options['experimental-host']
    ? requiredPath(options, 'experimental-host')
    : await selectThreeHostExecutable(
      buildDir, example, 'experimental', implementationAliases);
  const runDir = requiredPath(options, 'run-root');
  await stageThreeScenarioInputs([selectedExample], requiredPath(options, 'asset-root'));
  await fs.rm(runDir, { recursive: true, force: true });
  const report = await runThreeMatrix({
    sourceDir: repositoryRoot,
    runDir,
    profile: { hosts: { legacy: legacyHost, experimental: experimentalHost } },
    manifest,
    assetRoot: requiredPath(options, 'asset-root'),
    oracleRoot,
    status: 'phase1_required',
    group: 'full',
    pipelines,
    backends,
    repetitions: Number(options.repeat ?? 3),
    timeoutMs: Number(options['timeout-ms'] ?? 45_000),
    onProgress: (result, completed) => {
      const prefix = result.status === 'pass' ? 'PASS' : 'FAIL';
      console.log(`[${prefix}] ${completed} ${result.caseId}/${result.scenarioId} ${result.pipeline}/${result.backend} repeat=${result.repetition}`);
      for (const failure of result.failures) console.log(`  ${failure}`);
    }
  });
  report.schemaVersion = 1;
  report.caseId = caseId;
  report.evidenceType = 'strict-case-closure';
  report.samplePolicy = { mode: 'single-sample', msaaEnabled: false, simulateMsaa: false };
  // Preserve the explicit cardinalities required by the current strict-case
  // evidence reader.  runThreeMatrix keeps the detailed quadrant arrays but
  // intentionally leaves these aggregate fields to its caller.
  report.repeatCount = Number(options.repeat ?? 3);
  report.runCount = report.quadrants.length;
  report.expectedRunCount = report.quadrants.length;
  report.manifestPath = path.relative(repositoryRoot, manifestPath);
  // A candidate report is only useful as strict evidence when the generated
  // Legacy/Experimental artifacts and the Sample C++ GPU boundary have been
  // checked against the same source revision.  Keep the diagnostics in the
  // report instead of treating a visual-only pass as a completed case.
  if (buildDir) {
    const generatedArtifacts = lintGeneratedArtifacts({
      legacyRoot: path.join(buildDir, 'generated', 'three-r185', 'legacy'),
      experimentalRoot: path.join(buildDir, 'generated', 'three-r185', 'experimental'),
      manifestPath,
      caseIds: [caseId],
      fixtureSelectors: [],
      instancingPasses: []
    });
    report.generatedArtifacts = {
      status: generatedArtifacts.errors.length === 0 ? 'pass' : 'fail',
      checkedPasses: generatedArtifacts.checkedPasses,
      errors: generatedArtifacts.errors
    };
    if (generatedArtifacts.errors.length > 0) {
      report.status = 'fail';
      report.failures = [
        ...(report.failures ?? []),
        ...generatedArtifacts.errors.map((error) => (
          `generated-artifact lint ${error.code}: ${error.context}: ${error.message}`
        ))
      ];
    }
  }
  report.gpuBoundaryLint = await lintSampleGpuBoundary(repositoryRoot);
  if (report.gpuBoundaryLint.status !== 'pass') {
    report.status = 'fail';
    report.failures = [
      ...(report.failures ?? []),
      `sample GPU boundary lint found ${report.gpuBoundaryLint.violationCount} violation(s).`
    ];
  }
  report.generatedAt = new Date().toISOString();
  await writeJson(path.join(runDir, 'summary.json'), report);
  await writeReports(runDir, report);
  console.log(JSON.stringify({
    caseId,
    status: report.status,
    quadrantCount: report.quadrants.length,
    crossComparisonCount: report.crossComparisons.length,
    stabilityComparisonCount: report.stabilityComparisons.length,
    reportPath: path.relative(repositoryRoot, path.join(runDir, 'summary.json'))
  }, null, 2));
  if (report.status === 'fail') process.exitCode = 1;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
