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
  'GVMRuntime_ThreeSamples/Dsl/WebgpuTexturegather/WebgpuTexturegather.hpp'
);
const caseId = 'webgpu_texturegather';
const scenarioId = 'initial-comparison';
const pipelines = ['legacy', 'experimental'];
const backends = ['metal', 'vulkan'];
const repeatCount = 3;

/** Parses the explicit directories accepted by this dedicated gate. */
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
  for (const required of ['build-dir', 'oracle-root', 'output-dir']) {
    if (!options[required]) throw new Error(`Missing --${required}.`);
  }
  return options;
}

/** Executes one generated host with a bounded wall-clock duration. */
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

/** Loads one exact 800x500 RGBA8 capture. */
async function loadCapture(filePath) {
  const pixels = await fs.readFile(filePath);
  if (pixels.byteLength !== 800 * 500 * 4) {
    throw new Error(`${filePath} has ${pixels.byteLength} bytes; expected 1600000.`);
  }
  return { width: 800, height: 500, pixels };
}

/** Validates the private DSL and both generated pipeline contracts. */
async function lintGeneratedContracts(buildDirectory) {
  const failures = [];
  const source = await fs.readFile(sourcePath, 'utf8');
  for (const token of [
    'struct WebgpuTexturegatherSourceRenderSet : public IRenderSet',
    'class WebgpuTexturegatherWebgpuSourcePass final',
    'class WebgpuTexturegatherWebglSourcePass final',
    'class WebgpuTexturegatherMipPass final',
    'class WebgpuTexturegatherWebgpuMainPass final',
    'class WebgpuTexturegatherWebglMainPass final',
    '[[Export]] RenderSet<WebgpuTexturegatherSourceRenderSet> webgpuSourceSet',
    '[[Export]] RenderSet<WebgpuTexturegatherSourceRenderSet> webglSourceSet',
    '[[RenderEntityID]]',
    'gatherRed(',
    'gatheredDepth.x >= 1.0f',
    'webgpuSourcePass()',
    'webglSourcePass()',
    'webgpuMip6Color',
    'webglMip6Color'
  ]) {
    if (!source.includes(token)) failures.push(`DSL source is missing '${token}'.`);
  }
  if ((source.match(/struct WebgpuTexturegatherSourceRenderSet/g) ?? []).length !== 1) {
    failures.push('The two independent source Scenes must share one private Set type.');
  }
  if (/webgpuSourcePass\s*\(\s*\d/u.test(source) ||
      /webglSourcePass\s*\(\s*\d/u.test(source)) {
    failures.push('A source Scene pass uses an explicit draw count.');
  }
  for (const pipeline of pipelines) {
    const generatedRoot = path.join(
      buildDirectory,
      `generated/three-r185/${pipeline}/WebgpuTexturegather/UGLBin`
    );
    const generated = await fs.readFile(
      path.join(generatedRoot, 'generate_result.hpp'),
      'utf8'
    );
    for (const token of [
      'WebgpuTexturegatherSourceRenderSetComponents',
      'RenderEntityInfo',
      'WebgpuTexturegatherWebgpuSourcePass',
      'WebgpuTexturegatherWebglSourcePass',
      'WebgpuTexturegatherMipPass'
    ]) {
      if (!generated.includes(token)) {
        failures.push(`${pipeline} generated output is missing '${token}'.`);
      }
    }
    if (pipeline === 'experimental') {
      for (const artifact of [
        'uglir/WebgpuTexturegatherWebgpuMainPass__fragment.uglir.json',
        'msl/WebgpuTexturegatherWebgpuMainPass__fragment.msl',
        'spv/WebgpuTexturegatherWebgpuMainPass__fragment.spvasm'
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

/** Validates one capture identity and the two-Scene RenderSet snapshot. */
async function validateEvidence(run) {
  const metadata = JSON.parse(await fs.readFile(run.metadataPath, 'utf8'));
  const scene = JSON.parse(await fs.readFile(run.scenePath, 'utf8'));
  const semantic = JSON.parse(await fs.readFile(run.semanticPath, 'utf8'));
  const failures = [];
  if (metadata.caseId !== caseId || metadata.scenarioId !== scenarioId ||
      metadata.pipeline !== run.pipeline || metadata.backend !== run.backend ||
      metadata.frame !== 0) {
    failures.push('Capture identity differs from its requested quadrant.');
  }
  if (metadata.sampleCount !== 1 || metadata.msaaEnabled !== false ||
      metadata.byteCount !== 1_600_000 || metadata.format !== 'rgba8unorm') {
    failures.push('Capture violates the single-sample RGBA8 contract.');
  }
  if (scene.gpuWorkDslOnly !== true || scene.logicalSceneRootCount !== 4 ||
      scene.sourceSceneRenderSetCount !== 2 ||
      scene.renderSetType !== 'WebgpuTexturegatherSourceRenderSet' ||
      JSON.stringify(scene.sourceEntityCounts) !== '[1,1]' ||
      JSON.stringify(scene.sourceInstanceCounts) !== '[1,1]' ||
      scene.explicitColorMipPassCount !== 12 || scene.gpuPassCount !== 16 ||
      scene.usesColorGather !== true || scene.usesCompareGather !== true ||
      scene.compareGatherLowering !== 'component-gather-then-compare' ||
      JSON.stringify(scene.gatherOffset) !== '[0,7]' ||
      scene.sampleCount !== 1 || scene.msaaEnabled !== false) {
    failures.push('Scene snapshot violates the source-Set or gather contract.');
  }
  if (JSON.stringify(semantic.sourceSize) !== '[100,100]' ||
      semantic.sourceMipCount !== 7 || semantic.colorGatherChannel !== 0 ||
      semantic.depthCompareReference !== 1) {
    failures.push('Semantic snapshot differs from the frozen r185 gather state.');
  }
  return failures;
}

/** Executes four quadrants three times and retains all evidence. */
async function executeMatrix(options) {
  const buildDirectory = path.resolve(options['build-dir']);
  const outputDirectory = path.resolve(options['output-dir']);
  const runs = [];
  await fs.mkdir(outputDirectory, { recursive: true });
  for (const pipeline of pipelines) {
    for (const backend of backends) {
      const executable = path.join(
        buildDirectory,
        `gvm_three_samples/bin/WebgpuTexturegather-${pipeline}`
      );
      for (let repetition = 1; repetition <= repeatCount; repetition += 1) {
        const runDirectory = path.join(
          outputDirectory,
          `${pipeline}-${backend}-r${repetition}`
        );
        await fs.mkdir(runDirectory, { recursive: true });
        const run = {
          pipeline,
          backend,
          repetition,
          rgbaPath: path.join(runDirectory, 'capture.rgba'),
          metadataPath: path.join(runDirectory, 'capture.json'),
          scenePath: path.join(runDirectory, 'scene.json'),
          semanticPath: path.join(runDirectory, 'semantic.json')
        };
        await executeHost(executable, [
          '--case-id', caseId,
          '--scenario-id', scenarioId,
          '--pipeline', pipeline,
          '--backend', backend,
          '--width', '800',
          '--height', '500',
          '--frame', '0',
          '--random-seed', '305419896',
          '--capture-rgba', run.rgbaPath,
          '--capture-metadata', run.metadataPath,
          '--scene-snapshot', run.scenePath,
          '--semantic-snapshot', run.semanticPath
        ]);
        runs.push(run);
      }
    }
  }
  return runs;
}

/** Compares oracle, stability, and all required quadrant relations. */
async function compareMatrix(runs, oracleRoot) {
  const comparisons = [];
  const failures = [];
  const cache = new Map();
  const loadCached = async (filePath) => {
    if (!cache.has(filePath)) cache.set(filePath, loadCapture(filePath));
    return cache.get(filePath);
  };
  const oraclePath = path.join(oracleRoot, caseId, `${scenarioId}.rgba`);
  for (const run of runs) {
    failures.push(...(await validateEvidence(run)).map((failure) =>
      `${run.pipeline}/${run.backend}/r${run.repetition}: ${failure}`
    ));
    const result = compareThreeCaptures(
      await loadCached(oraclePath),
      await loadCached(run.rgbaPath)
    );
    comparisons.push({ kind: 'oracle', run, ...result });
    failures.push(...result.failures.map((failure) =>
      `oracle ${run.pipeline}/${run.backend}/r${run.repetition}: ${failure}`
    ));
  }
  for (const pipeline of pipelines) {
    for (const backend of backends) {
      const candidates = runs.filter((run) =>
        run.pipeline === pipeline && run.backend === backend
      );
      for (let index = 1; index < candidates.length; index += 1) {
        const result = compareThreeCaptures(
          await loadCached(candidates[0].rgbaPath),
          await loadCached(candidates[index].rgbaPath)
        );
        comparisons.push({
          kind: 'stability',
          left: candidates[0],
          right: candidates[index],
          ...result
        });
        failures.push(...result.failures.map((failure) =>
          `stability ${pipeline}/${backend}: ${failure}`
        ));
      }
    }
  }
  const relations = [
    ['legacy', 'metal', 'experimental', 'metal'],
    ['legacy', 'vulkan', 'experimental', 'vulkan'],
    ['legacy', 'metal', 'legacy', 'vulkan'],
    ['experimental', 'metal', 'experimental', 'vulkan']
  ];
  for (let repetition = 1; repetition <= repeatCount; repetition += 1) {
    for (const relation of relations) {
      const [leftPipeline, leftBackend, rightPipeline, rightBackend] = relation;
      const left = runs.find((run) => run.pipeline === leftPipeline &&
        run.backend === leftBackend && run.repetition === repetition);
      const right = runs.find((run) => run.pipeline === rightPipeline &&
        run.backend === rightBackend && run.repetition === repetition);
      const result = compareThreeCaptures(
        await loadCached(left.rgbaPath),
        await loadCached(right.rgbaPath)
      );
      comparisons.push({ kind: 'cross-quadrant', left, right, ...result });
      failures.push(...result.failures.map((failure) =>
        `cross r${repetition}/${leftPipeline}-${leftBackend}:` +
        `${rightPipeline}-${rightBackend}: ${failure}`
      ));
    }
  }
  return { comparisons, failures };
}

/** Runs the formal gate and writes its deterministic aggregate summary. */
async function main() {
  const options = parseArguments(process.argv.slice(2));
  const buildDirectory = path.resolve(options['build-dir']);
  const outputDirectory = path.resolve(options['output-dir']);
  const lintFailures = await lintGeneratedContracts(buildDirectory);
  const runs = await executeMatrix(options);
  const result = await compareMatrix(runs, path.resolve(options['oracle-root']));
  const failures = [...lintFailures, ...result.failures];
  const summary = {
    schemaVersion: 1,
    caseId,
    status: failures.length === 0 ? 'pass' : 'fail',
    executionCount: runs.length,
    oracleComparisonCount: result.comparisons.filter((item) => item.kind === 'oracle').length,
    stabilityComparisonCount: result.comparisons.filter((item) => item.kind === 'stability').length,
    crossQuadrantComparisonCount: result.comparisons.filter((item) => item.kind === 'cross-quadrant').length,
    lintFailureCount: lintFailures.length,
    singleSamplePolicy: {
      sampleCount: 1,
      msaaEnabled: false,
      simulateMsaa: false
    },
    runs,
    comparisons: result.comparisons,
    failures
  };
  const summaryPath = path.join(outputDirectory, 'summary.json');
  await fs.writeFile(summaryPath, `${JSON.stringify(summary, null, 2)}\n`);
  console.log(`webgpu_texturegather formal gate: ${summary.status}`);
  console.log(
    `executions=${summary.executionCount} oracle=${summary.oracleComparisonCount} ` +
    `stability=${summary.stabilityComparisonCount} cross=${summary.crossQuadrantComparisonCount}`
  );
  console.log(`summary=${summaryPath}`);
  if (failures.length > 0) {
    for (const failure of failures) console.error(`- ${failure}`);
    process.exitCode = 1;
  }
}

await main();
