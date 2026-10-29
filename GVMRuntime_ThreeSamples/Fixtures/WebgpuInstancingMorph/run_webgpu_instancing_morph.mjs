import { spawn } from 'node:child_process';
import fs from 'node:fs/promises';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

import {
  compareThreeCaptures
} from '../../../tests/runners/three/node/image-comparison.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');
const sourcePath = path.join(
  repositoryRoot,
  'GVMRuntime_ThreeSamples/Dsl/WebgpuInstancingMorph/WebgpuInstancingMorph.hpp'
);
const scenarios = [
  { id: 'loader-snapshot', frame: 0 },
  { id: 'initial-loader', frame: 0 },
  { id: 'animated', frame: 60 }
];
const pipelines = ['legacy', 'experimental'];
const backends = ['metal', 'vulkan'];
const repeatCount = 3;

/** Parses explicit key/value options accepted by the dedicated formal runner. */
function parseArguments(argv) {
  const options = {};
  for (let index = 0; index < argv.length; index += 2) {
    const key = argv[index];
    const value = argv[index + 1];
    if (!key?.startsWith('--') || value == null) {
      throw new Error(`Invalid runner argument near '${key ?? '<end>'}'.`);
    }
    options[key.slice(2)] = value;
  }
  for (const required of ['build-dir', 'asset-root', 'oracle-root', 'output-dir']) {
    if (!options[required]) throw new Error(`Missing --${required}.`);
  }
  return options;
}

/** Executes one generated host and returns its bounded stdout and stderr. */
function executeHost(executable, argumentsList) {
  return new Promise((resolve, reject) => {
    const child = spawn(executable, argumentsList, {
      cwd: repositoryRoot,
      stdio: ['ignore', 'pipe', 'pipe']
    });
    let stdout = '';
    let stderr = '';
    child.stdout.on('data', (chunk) => { stdout += chunk; });
    child.stderr.on('data', (chunk) => { stderr += chunk; });
    const timeout = setTimeout(() => {
      child.kill('SIGKILL');
      reject(new Error(`Host timed out: ${executable}`));
    }, 60_000);
    child.on('error', reject);
    child.on('close', (code, signal) => {
      clearTimeout(timeout);
      if (code !== 0) {
        reject(new Error(
          `Host failed (${code ?? signal}): ${executable}\n${stderr}\n${stdout}`
        ));
        return;
      }
      resolve({ stdout, stderr });
    });
  });
}

/** Loads one exact 800x500 RGBA8 artifact for immutable comparison. */
async function loadCapture(filePath) {
  const pixels = await fs.readFile(filePath);
  if (pixels.byteLength !== 800 * 500 * 4) {
    throw new Error(`${filePath} has ${pixels.byteLength} bytes; expected 1600000.`);
  }
  return { width: 800, height: 500, pixels };
}

/** Validates the DSL source and both generated pipeline contracts. */
async function lintGeneratedContracts(buildDirectory) {
  const failures = [];
  const source = await fs.readFile(sourcePath, 'utf8');
  const requiredSourceTokens = [
    'struct WebgpuInstancingMorphSceneRenderSet : public IRenderSet',
    'class WebgpuInstancingMorphShadowPass final',
    'class WebgpuInstancingMorphMainPass final',
    'class WebgpuInstancingMorphInspectorPass final',
    '[[Export]] RenderSet<WebgpuInstancingMorphSceneRenderSet> sceneSet',
    '[[RenderEntityID]]',
    '[[RenderEntityInstanceID]]',
    'sceneSet->morphTargets->get',
    'shadowPass()',
    'mainPass()',
    'inspectorPass(3u, 1u, 0u, 0u)'
  ];
  for (const token of requiredSourceTokens) {
    if (!source.includes(token)) failures.push(`DSL source is missing '${token}'.`);
  }
  if ((source.match(/struct WebgpuInstancingMorphSceneRenderSet/g) ?? []).length !== 1) {
    failures.push('The logical Scene must declare exactly one dedicated RenderSet type.');
  }
  if (/mainPass\s*\(\s*\d/u.test(source) || /shadowPass\s*\(\s*\d/u.test(source)) {
    failures.push('A Scene pass uses an explicit draw count instead of Set-only draw.');
  }
  for (const pipeline of pipelines) {
    const generatedRoot = path.join(
      buildDirectory,
      `generated/three-r185/${pipeline}/WebgpuInstancingMorph/UGLBin`
    );
    const generated = await fs.readFile(path.join(generatedRoot, 'generate_result.hpp'), 'utf8');
    for (const token of [
      'WebgpuInstancingMorphSceneRenderSetComponents',
      'RenderEntityInfo',
      'WebgpuInstancingMorphShadowPass',
      'WebgpuInstancingMorphMainPass'
    ]) {
      if (!generated.includes(token)) {
        failures.push(`${pipeline} generated output is missing '${token}'.`);
      }
    }
    if (pipeline === 'experimental') {
      for (const artifact of [
        'uglir/WebgpuInstancingMorphMainPass__vertex.uglir.json',
        'msl/WebgpuInstancingMorphMainPass__vertex.msl',
        'spv/WebgpuInstancingMorphMainPass__vertex.spvasm'
      ]) {
        try {
          await fs.access(path.join(generatedRoot, artifact));
        } catch {
          failures.push(`Experimental generated artifact is missing: ${artifact}.`);
        }
      }
    }
  }
  return failures;
}

/** Validates capture metadata and the one-Scene one-Set runtime snapshot. */
async function validateEvidence(run) {
  const metadata = JSON.parse(await fs.readFile(run.metadataPath, 'utf8'));
  const scene = JSON.parse(await fs.readFile(run.scenePath, 'utf8'));
  const failures = [];
  if (metadata.caseId !== 'webgpu_instancing_morph' ||
      metadata.scenarioId !== run.scenario || metadata.pipeline !== run.pipeline ||
      metadata.backend !== run.backend || metadata.frame !== run.frame) {
    failures.push('Capture identity metadata differs from its requested quadrant.');
  }
  if (metadata.sampleCount !== 1 || metadata.msaaEnabled !== false ||
      metadata.byteCount !== 1_600_000 || metadata.format !== 'rgba8unorm') {
    failures.push('Capture metadata violates the fixed single-sample RGBA8 contract.');
  }
  if (scene.renderSetPolicy !== 'required' || scene.sceneRenderSetCount !== 1 ||
      scene.renderSetType !== 'WebgpuInstancingMorphSceneRenderSet' ||
      scene.entityCount !== 2 || JSON.stringify(scene.instanceCounts) !== '[1,1024]' ||
      scene.drawCommandCount !== 2 || scene.scenePassCount !== 2 ||
      scene.usesRenderEntityID !== true || scene.usesRenderEntityInstanceID !== true ||
      scene.gpuWorkDslOnly !== true || scene.sampleCount !== 1 ||
      scene.msaaEnabled !== false) {
    failures.push('Scene snapshot violates the unique RenderSet or entity contract.');
  }
  return failures;
}

/** Runs every scenario, quadrant, and repetition and records immutable artifacts. */
async function executeMatrix(options) {
  const buildDirectory = path.resolve(options['build-dir']);
  const outputDirectory = path.resolve(options['output-dir']);
  const assetRoot = path.resolve(options['asset-root']);
  const runs = [];
  await fs.mkdir(outputDirectory, { recursive: true });
  for (const scenario of scenarios) {
    for (const pipeline of pipelines) {
      for (const backend of backends) {
        const executable = path.join(
          buildDirectory,
          `gvm_three_samples/bin/WebgpuInstancingMorph-${pipeline}`
        );
        for (let repetition = 1; repetition <= repeatCount; repetition += 1) {
          const runDirectory = path.join(
            outputDirectory,
            scenario.id,
            `${pipeline}-${backend}-r${repetition}`
          );
          await fs.mkdir(runDirectory, { recursive: true });
          const run = {
            scenario: scenario.id,
            frame: scenario.frame,
            pipeline,
            backend,
            repetition,
            rgbaPath: path.join(runDirectory, 'capture.rgba'),
            metadataPath: path.join(runDirectory, 'capture.json'),
            scenePath: path.join(runDirectory, 'scene.json'),
            semanticPath: path.join(runDirectory, 'semantic.json')
          };
          await executeHost(executable, [
            '--case-id', 'webgpu_instancing_morph',
            '--scenario-id', scenario.id,
            '--pipeline', pipeline,
            '--backend', backend,
            '--width', '800',
            '--height', '500',
            '--frame', String(scenario.frame),
            '--random-seed', '305419896',
            '--asset-root', assetRoot,
            '--capture-rgba', run.rgbaPath,
            '--capture-metadata', run.metadataPath,
            '--scene-snapshot', run.scenePath,
            '--semantic-snapshot', run.semanticPath
          ]);
          runs.push(run);
        }
      }
    }
  }
  return runs;
}

/** Compares all oracle, stability, and cross-quadrant relations. */
async function compareMatrix(runs, oracleRoot) {
  const comparisons = [];
  const failures = [];
  const captureCache = new Map();
  const loadCached = async (filePath) => {
    if (!captureCache.has(filePath)) captureCache.set(filePath, loadCapture(filePath));
    return captureCache.get(filePath);
  };
  for (const run of runs) {
    failures.push(...(await validateEvidence(run)).map((failure) =>
      `${run.scenario}/${run.pipeline}/${run.backend}/r${run.repetition}: ${failure}`
    ));
    const oraclePath = path.join(oracleRoot, 'webgpu_instancing_morph', `${run.scenario}.rgba`);
    const result = compareThreeCaptures(
      await loadCached(oraclePath),
      await loadCached(run.rgbaPath)
    );
    comparisons.push({ kind: 'oracle', run, ...result });
    failures.push(...result.failures.map((failure) =>
      `oracle ${run.scenario}/${run.pipeline}/${run.backend}/r${run.repetition}: ${failure}`
    ));
  }
  for (const scenario of scenarios) {
    for (const pipeline of pipelines) {
      for (const backend of backends) {
        const candidates = runs.filter((run) =>
          run.scenario === scenario.id && run.pipeline === pipeline && run.backend === backend
        );
        for (let index = 1; index < candidates.length; index += 1) {
          const result = compareThreeCaptures(
            await loadCached(candidates[0].rgbaPath),
            await loadCached(candidates[index].rgbaPath)
          );
          comparisons.push({ kind: 'stability', left: candidates[0], right: candidates[index], ...result });
          failures.push(...result.failures.map((failure) =>
            `stability ${scenario.id}/${pipeline}/${backend}: ${failure}`
          ));
        }
      }
    }
    const relations = [
      ['legacy-metal', 'experimental-metal'],
      ['legacy-vulkan', 'experimental-vulkan'],
      ['legacy-metal', 'legacy-vulkan'],
      ['experimental-metal', 'experimental-vulkan']
    ];
    for (let repetition = 1; repetition <= repeatCount; repetition += 1) {
      for (const [leftName, rightName] of relations) {
        const [leftPipeline, leftBackend] = leftName.split('-');
        const [rightPipeline, rightBackend] = rightName.split('-');
        const left = runs.find((run) => run.scenario === scenario.id &&
          run.pipeline === leftPipeline && run.backend === leftBackend &&
          run.repetition === repetition);
        const right = runs.find((run) => run.scenario === scenario.id &&
          run.pipeline === rightPipeline && run.backend === rightBackend &&
          run.repetition === repetition);
        const result = compareThreeCaptures(
          await loadCached(left.rgbaPath),
          await loadCached(right.rgbaPath)
        );
        comparisons.push({ kind: 'cross-quadrant', left, right, ...result });
        failures.push(...result.failures.map((failure) =>
          `cross ${scenario.id}/r${repetition}/${leftName}:${rightName}: ${failure}`
        ));
      }
    }
  }
  return { comparisons, failures };
}

/** Executes the formal gate and writes one deterministic aggregate summary. */
async function main() {
  const options = parseArguments(process.argv.slice(2));
  const buildDirectory = path.resolve(options['build-dir']);
  const outputDirectory = path.resolve(options['output-dir']);
  const lintFailures = await lintGeneratedContracts(buildDirectory);
  const runs = await executeMatrix(options);
  const comparisonResult = await compareMatrix(
    runs,
    path.resolve(options['oracle-root'])
  );
  const failures = [...lintFailures, ...comparisonResult.failures];
  const summary = {
    schemaVersion: 1,
    caseId: 'webgpu_instancing_morph',
    status: failures.length === 0 ? 'pass' : 'fail',
    executionCount: runs.length,
    oracleComparisonCount: comparisonResult.comparisons.filter((item) => item.kind === 'oracle').length,
    stabilityComparisonCount: comparisonResult.comparisons.filter((item) => item.kind === 'stability').length,
    crossQuadrantComparisonCount: comparisonResult.comparisons.filter((item) => item.kind === 'cross-quadrant').length,
    lintFailureCount: lintFailures.length,
    singleSamplePolicy: { sampleCount: 1, msaaEnabled: false, simulateMsaa: false },
    runs,
    comparisons: comparisonResult.comparisons,
    failures
  };
  const summaryPath = path.join(outputDirectory, 'summary.json');
  await fs.writeFile(summaryPath, `${JSON.stringify(summary, null, 2)}\n`);
  console.log(`webgpu_instancing_morph formal gate: ${summary.status}`);
  console.log(`executions=${summary.executionCount} oracle=${summary.oracleComparisonCount} stability=${summary.stabilityComparisonCount} cross=${summary.crossQuadrantComparisonCount}`);
  console.log(`summary=${summaryPath}`);
  if (failures.length > 0) {
    for (const failure of failures) console.error(`- ${failure}`);
    process.exitCode = 1;
  }
}

await main();
