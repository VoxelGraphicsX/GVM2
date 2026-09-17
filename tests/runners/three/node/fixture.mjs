import { constants as fsConstants, promises as fs } from 'node:fs';
import path from 'node:path';

import { defaultThreeRandomSeed } from './determinism.mjs';
import { compareThreeCaptures, comparisonThresholds, loadRgbaArtifact } from './image-comparison.mjs';
import { executeWithWatchdog, loadJson, writeJson } from './runner.mjs';

export const renderSetPhase0Fixture = Object.freeze({
  caseId: 'render-set-phase0',
  scenarioId: 'entity-lifecycle',
  frame: 1,
  width: 800,
  height: 500,
  pipelines: Object.freeze(['legacy', 'uglir']),
  backends: Object.freeze(['metal', 'vulkan']),
  componentSchema: Object.freeze(['vertices', 'indices', 'objects', 'instances', 'materials', 'albedo'])
});

/** Returns true when a host path exists and is executable by the current process. */
async function isExecutable(filePath) {
  try {
    await fs.access(filePath, fsConstants.X_OK);
    return true;
  } catch {
    return false;
  }
}

/** Resolves the fixed Phase 0 pipeline host from an explicit override or build output. */
export async function resolveFixtureHost(buildDir, hosts, pipeline) {
  const explicit = hosts?.[pipeline];
  const defaultHost = path.join(
    buildDir,
    'gvm_three_samples',
    'bin',
    `RenderSetPhase0-${pipeline}`
  );
  const candidate = explicit || defaultHost;
  if (!await isExecutable(candidate)) {
    throw new Error(`Missing executable RenderSet Phase 0 ${pipeline} host: ${candidate}.`);
  }
  return candidate;
}

/** Constructs deterministic artifact paths for one Phase 0 matrix quadrant. */
export function makeFixtureArtifactPaths(runDir, pipeline, backend) {
  const artifactDir = path.join(runDir, 'artifacts', pipeline, backend);
  return {
    artifactDir,
    rgbaPath: path.join(artifactDir, 'final.rgba'),
    metadataPath: path.join(artifactDir, 'final.json'),
    sceneSnapshotPath: path.join(artifactDir, 'scene-snapshot.json'),
    hostLogPath: path.join(artifactDir, 'host.json')
  };
}

/** Builds the complete explicit CLI contract for one Phase 0 host invocation. */
export function buildFixtureHostArguments(pipeline, backend, artifactPaths) {
  return [
    '--case-id', renderSetPhase0Fixture.caseId,
    '--scenario-id', renderSetPhase0Fixture.scenarioId,
    '--frame', String(renderSetPhase0Fixture.frame),
    '--random-seed', String(defaultThreeRandomSeed),
    '--pipeline', pipeline,
    '--backend', backend,
    '--width', String(renderSetPhase0Fixture.width),
    '--height', String(renderSetPhase0Fixture.height),
    '--capture-rgba', artifactPaths.rgbaPath,
    '--capture-metadata', artifactPaths.metadataPath,
    '--scene-snapshot', artifactPaths.sceneSnapshotPath
  ];
}

/** Validates deterministic RGBA metadata emitted by one Phase 0 host. */
export function validateFixtureMetadata(metadata, pipeline, backend) {
  const failures = [];
  const expectedByteCount = renderSetPhase0Fixture.width * renderSetPhase0Fixture.height * 4;
  const expectedValues = [
    ['caseId', renderSetPhase0Fixture.caseId],
    ['scenarioId', renderSetPhase0Fixture.scenarioId],
    ['pipeline', pipeline],
    ['backend', backend],
    ['frame', renderSetPhase0Fixture.frame],
    ['randomSeed', defaultThreeRandomSeed],
    ['width', renderSetPhase0Fixture.width],
    ['height', renderSetPhase0Fixture.height],
    ['rowStrideBytes', renderSetPhase0Fixture.width * 4],
    ['byteCount', expectedByteCount],
    ['format', 'rgba8unorm']
  ];
  for (const [field, expected] of expectedValues) {
    if (metadata?.[field] !== expected) {
      failures.push(`capture metadata ${field}=${JSON.stringify(metadata?.[field])}; expected ${JSON.stringify(expected)}.`);
    }
  }
  return failures;
}

/** Counts pixels whose RGB value differs from the first captured pixel. */
function countNonUniformRgbPixels(image) {
  const firstRed = image.pixels[0];
  const firstGreen = image.pixels[1];
  const firstBlue = image.pixels[2];
  let nonUniformPixels = 0;
  for (let offset = 4; offset < image.pixels.length; offset += 4) {
    if (image.pixels[offset] !== firstRed
        || image.pixels[offset + 1] !== firstGreen
        || image.pixels[offset + 2] !== firstBlue) {
      nonUniformPixels += 1;
    }
  }
  return nonUniformPixels;
}

/** Counts distinct RGB values without treating alpha-only changes as rendered geometry. */
function countUniqueRgbColors(image) {
  const colors = new Set();
  for (let offset = 0; offset < image.pixels.length; offset += 4) {
    colors.add((image.pixels[offset] << 16)
      | (image.pixels[offset + 1] << 8)
      | image.pixels[offset + 2]);
  }
  return colors.size;
}

/** Counts reserved magenta pixels emitted when the shader bounds probe fails. */
function countBoundsSafetyFailurePixels(image) {
  let failurePixels = 0;
  for (let offset = 0; offset < image.pixels.length; offset += 4) {
    if (image.pixels[offset] === 255
        && image.pixels[offset + 1] === 0
        && image.pixels[offset + 2] === 255) {
      failurePixels += 1;
    }
  }
  return failurePixels;
}

/** Rejects a capture that contains only its clear color. */
export function validateFixtureImage(image) {
  const nonUniformRgbPixels = countNonUniformRgbPixels(image);
  const uniqueRgbColorCount = countUniqueRgbColors(image);
  const boundsSafetyFailurePixels = countBoundsSafetyFailurePixels(image);
  const failures = [];
  if (nonUniformRgbPixels === 0) {
    failures.push('RGBA capture is clear-only: every RGB pixel equals the first pixel.');
  }
  if (uniqueRgbColorCount < 5) {
    failures.push(`RGBA capture has ${uniqueRgbColorCount} unique RGB colors; the fixture requires at least 5 after entity mutation.`);
  }
  if (boundsSafetyFailurePixels > 0) {
    failures.push(`RGBA capture contains ${boundsSafetyFailurePixels} reserved magenta pixels from a failed out-of-bounds component safety probe.`);
  }
  return {
    failures,
    nonUniformRgbPixels,
    uniqueRgbColorCount,
    boundsSafetyFailurePixels,
    firstPixelRgba: [...image.pixels.slice(0, 4)]
  };
}

/** Returns true for a concrete RenderSet entity index rather than the invalid sentinel. */
function isValidEntityIndex(value) {
  return Number.isInteger(value) && value >= 0 && value < 0xffffffff;
}

/** Validates the exact RenderSet entity lifecycle recorded by the Phase 0 fixture. */
export function validateFixtureSnapshot(snapshot) {
  const failures = [];
  if (snapshot?.sceneRenderSetCount !== 1) {
    failures.push(`sceneRenderSetCount must be 1; observed ${JSON.stringify(snapshot?.sceneRenderSetCount)}.`);
  }
  if (snapshot?.entityCount !== 2) {
    failures.push(`entityCount must be 2; observed ${JSON.stringify(snapshot?.entityCount)}.`);
  }
  if (snapshot?.drawCommandCount !== 1) {
    failures.push(`drawCommandCount must be 1; observed ${JSON.stringify(snapshot?.drawCommandCount)}.`);
  }
  if (snapshot?.mutationApplied !== true) {
    failures.push('remove/reallocate mutation was not applied before capture.');
  }

  const entities = Array.isArray(snapshot?.entities) ? snapshot.entities : [];
  if (entities.length !== 2) {
    failures.push(`entities must contain exactly 2 live records; observed ${entities.length}.`);
  }
  const instanced = entities.filter((entity) => entity?.role === 'instanced');
  const replacement = entities.filter((entity) => entity?.role === 'replacement');
  if (instanced.length !== 1 || !Number.isInteger(instanced[0]?.instanceCount) || instanced[0].instanceCount < 3) {
    failures.push('snapshot must contain one instanced entity with instanceCount >= 3.');
  }
  if (replacement.length !== 1 || replacement[0]?.instanceCount !== 1) {
    failures.push('snapshot must contain one replacement entity with instanceCount = 1.');
  }

  const removedEntity = snapshot?.removedEntity;
  const replacementEntity = snapshot?.replacementEntity;
  if (!isValidEntityIndex(removedEntity) || !isValidEntityIndex(replacementEntity)) {
    failures.push('removedEntity and replacementEntity must be valid allocated entity indices.');
  }
  if (replacement.length === 1 && replacement[0]?.entity !== replacementEntity) {
    failures.push('replacement live record does not match replacementEntity.');
  }
  const entityIds = entities.map((entity) => entity?.entity);
  if (new Set(entityIds).size !== entityIds.length) {
    failures.push('live entity records contain a duplicate entity index.');
  }
  const expectedReuse = isValidEntityIndex(removedEntity)
    && isValidEntityIndex(replacementEntity)
    && removedEntity === replacementEntity;
  if (snapshot?.reusedEntityIndex !== expectedReuse) {
    failures.push('reusedEntityIndex does not match the remove/reallocate entity indices.');
  }
  if (!expectedReuse && entityIds.includes(removedEntity)) {
    failures.push('removed entity leaked into the live entity list after reallocation.');
  }
  if (entities.some((entity) => entity?.role === 'initial' || entity?.role === 'removed')) {
    failures.push('old entity role leaked into the live entity list after reallocation.');
  }

  const componentSchema = Array.isArray(snapshot?.componentSchema) ? snapshot.componentSchema : [];
  for (const componentName of renderSetPhase0Fixture.componentSchema) {
    if (!componentSchema.includes(componentName)) {
      failures.push(`componentSchema is missing required fixture component '${componentName}'.`);
    }
  }
  return failures;
}

/** Loads and validates every output produced by one successful Phase 0 host. */
export async function validateFixtureArtifacts(artifactPaths, pipeline, backend) {
  const [image, snapshot] = await Promise.all([
    loadRgbaArtifact(artifactPaths.rgbaPath, artifactPaths.metadataPath),
    loadJson(artifactPaths.sceneSnapshotPath)
  ]);
  const metadataFailures = validateFixtureMetadata(image.metadata, pipeline, backend);
  const imageValidation = validateFixtureImage(image);
  const snapshotFailures = validateFixtureSnapshot(snapshot);
  return {
    image,
    metadata: image.metadata,
    snapshot,
    imageValidation,
    failures: [...metadataFailures, ...imageValidation.failures, ...snapshotFailures]
  };
}

/** Runs one Phase 0 pipeline/backend quadrant under a hard watchdog. */
export async function runFixtureQuadrant(context, pipeline, backend) {
  const artifactPaths = makeFixtureArtifactPaths(context.runDir, pipeline, backend);
  await fs.mkdir(artifactPaths.artifactDir, { recursive: true });
  const startedAt = new Date();
  const result = {
    caseId: renderSetPhase0Fixture.caseId,
    scenarioId: renderSetPhase0Fixture.scenarioId,
    pipeline,
    backend,
    repetition: 1,
    status: 'fail',
    startedAt: startedAt.toISOString(),
    durationMs: 0,
    artifacts: artifactPaths,
    failures: []
  };
  try {
    const host = await resolveFixtureHost(context.buildDir, context.hosts, pipeline);
    const args = buildFixtureHostArguments(pipeline, backend, artifactPaths);
    const executeHost = context.executeHost ?? executeWithWatchdog;
    const execution = await executeHost(host, args, {
      cwd: context.sourceDir,
      timeoutMs: context.timeoutMs
    });
    result.host = { executable: host, args, ...execution };
    await writeJson(artifactPaths.hostLogPath, result.host);
    if (execution.timedOut) {
      result.failures.push(`Host exceeded ${context.timeoutMs} ms watchdog.`);
    } else if (execution.exitCode !== 0) {
      result.failures.push(`Host exited with code ${execution.exitCode}${execution.signal ? ` (${execution.signal})` : ''}.`);
    } else {
      const validation = await validateFixtureArtifacts(artifactPaths, pipeline, backend);
      result.validation = {
        metadata: validation.metadata,
        snapshot: validation.snapshot,
        imageValidation: validation.imageValidation
      };
      result.failures.push(...validation.failures);
    }
  } catch (error) {
    result.failures.push(error instanceof Error ? error.message : String(error));
  }
  result.status = result.failures.length === 0 ? 'pass' : 'fail';
  result.durationMs = Date.now() - startedAt.getTime();
  return result;
}

/** Compares two Phase 0 captures even when a non-image quadrant assertion failed. */
export async function compareFixtureQuadrantPair(left, right, relation) {
  const comparison = {
    relation,
    left: { pipeline: left.pipeline, backend: left.backend },
    right: { pipeline: right.pipeline, backend: right.backend },
    status: 'fail',
    failures: []
  };
  try {
    const [leftImage, rightImage] = await Promise.all([
      loadRgbaArtifact(left.artifacts.rgbaPath, left.artifacts.metadataPath),
      loadRgbaArtifact(right.artifacts.rgbaPath, right.artifacts.metadataPath)
    ]);
    const result = compareThreeCaptures(leftImage, rightImage);
    comparison.metrics = result.metrics;
    comparison.failures.push(...result.failures);
  } catch (error) {
    comparison.failures.push(error instanceof Error ? error.message : String(error));
  }
  comparison.status = comparison.failures.length === 0 ? 'pass' : 'fail';
  return comparison;
}

/** Builds the four mandatory Legacy/Experimental and Metal/Vulkan parity pairs. */
export async function buildFixtureCrossComparisons(quadrants) {
  const byKey = new Map(quadrants.map((quadrant) => [
    `${quadrant.pipeline}|${quadrant.backend}`,
    quadrant
  ]));
  const definitions = [
    ['legacy', 'metal', 'uglir', 'metal', 'pipeline-parity-metal'],
    ['legacy', 'vulkan', 'uglir', 'vulkan', 'pipeline-parity-vulkan'],
    ['legacy', 'metal', 'legacy', 'vulkan', 'backend-parity-legacy'],
    ['uglir', 'metal', 'uglir', 'vulkan', 'backend-parity-experimental']
  ];
  const comparisons = [];
  for (const [leftPipeline, leftBackend, rightPipeline, rightBackend, relation] of definitions) {
    const left = byKey.get(`${leftPipeline}|${leftBackend}`);
    const right = byKey.get(`${rightPipeline}|${rightBackend}`);
    if (left === undefined || right === undefined) {
      comparisons.push({
        relation,
        status: 'fail',
        failures: ['Required fixture quadrant is missing.']
      });
    } else {
      comparisons.push(await compareFixtureQuadrantPair(left, right, relation));
    }
  }
  return comparisons;
}

/** Runs all four Phase 0 quadrants and returns a machine-readable gate report. */
export async function runRenderSetPhase0Fixture(context) {
  const quadrants = [];
  for (const pipeline of renderSetPhase0Fixture.pipelines) {
    for (const backend of renderSetPhase0Fixture.backends) {
      const quadrant = await runFixtureQuadrant(context, pipeline, backend);
      quadrants.push(quadrant);
      context.onProgress?.(quadrant, quadrants.length);
    }
  }
  const crossComparisons = await buildFixtureCrossComparisons(quadrants);
  const status = quadrants.every((quadrant) => quadrant.status === 'pass')
    && crossComparisons.every((comparison) => comparison.status === 'pass')
    ? 'pass'
    : 'fail';
  return {
    schemaVersion: 1,
    gate: 'render-set-phase0-four-quadrant',
    status,
    fixture: renderSetPhase0Fixture,
    thresholds: comparisonThresholds,
    quadrants,
    crossComparisons
  };
}
