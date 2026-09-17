import { spawn } from 'node:child_process';
import { promises as fs } from 'node:fs';
import path from 'node:path';

import { defaultThreeRandomSeed } from './determinism.mjs';

import {
  compareThreeCaptures,
  comparisonThresholds,
  loadRgbaArtifact
} from './image-comparison.mjs';

export const requiredPipelines = Object.freeze(['legacy', 'uglir']);
export const requiredBackends = Object.freeze(['metal', 'vulkan']);

/** Returns true when a path exists and is accessible. */
export async function pathExists(targetPath) {
  try {
    await fs.access(targetPath);
    return true;
  } catch {
    return false;
  }
}

/** Loads and parses one UTF-8 JSON document. */
export async function loadJson(filePath) {
  return JSON.parse(await fs.readFile(filePath, 'utf8'));
}

/** Writes one stable, human-readable JSON document after creating its parent directory. */
export async function writeJson(filePath, value) {
  await fs.mkdir(path.dirname(filePath), { recursive: true });
  await fs.writeFile(filePath, `${JSON.stringify(value, null, 2)}\n`, 'utf8');
}

/** Converts a command-line status spelling to the manifest spelling. */
export function normalizeStatus(value) {
  return String(value ?? 'phase1_required').trim().toLowerCase().replaceAll('-', '_');
}

/** Expands a single or all selector against one immutable allowed value list. */
export function expandSelector(value, allowed, label) {
  const normalized = String(value ?? 'all').trim().toLowerCase();
  if (normalized === 'all') {
    return [...allowed];
  }
  if (!allowed.includes(normalized)) {
    throw new Error(`Unsupported ${label} '${value}'. Expected ${allowed.join(', ')}, or all.`);
  }
  return [normalized];
}

/** Converts arbitrary case and scenario identifiers to safe path segments. */
export function sanitizeSegment(value) {
  const result = String(value ?? '').trim()
    .replaceAll(/[^a-zA-Z0-9._-]+/gu, '-')
    .replaceAll(/-+/gu, '-')
    .replaceAll(/^-|-$/gu, '');
  return result || 'unnamed';
}

/** Returns a sortable local run timestamp without relying on runtime environment variables. */
export function makeRunTimestamp(date = new Date()) {
  const components = [
    date.getFullYear(),
    String(date.getMonth() + 1).padStart(2, '0'),
    String(date.getDate()).padStart(2, '0')
  ];
  const time = [
    String(date.getHours()).padStart(2, '0'),
    String(date.getMinutes()).padStart(2, '0'),
    String(date.getSeconds()).padStart(2, '0'),
    String(date.getMilliseconds()).padStart(3, '0')
  ];
  return `${components.join('')}_${time.join('')}`;
}

/** Selects manifest cases and enforces that Phase 1 classification has been explicitly locked. */
export function selectCases(manifest, requestedStatus, group = 'full') {
  const status = normalizeStatus(requestedStatus);
  const examples = Array.isArray(manifest.examples) ? manifest.examples : [];
  if (examples.length !== 588) {
    throw new Error(`Manifest must contain exactly 588 Three r185 examples; found ${examples.length}.`);
  }
  const pendingCount = examples.filter((example) => example.status === 'audit_pending').length;
  if (status === 'phase1_required' && pendingCount > 0) {
    throw new Error(`Phase 0 capability audit is not locked: ${pendingCount} candidate examples remain audit_pending.`);
  }

  let selected = examples.filter((example) => example.status === status);
  if (group !== 'full') {
    const match = /^shard-(\d+)-of-(\d+)$/u.exec(group);
    if (!match) {
      throw new Error(`Unsupported group '${group}'. Expected full or shard-N-of-M.`);
    }
    const shardIndex = Number(match[1]);
    const shardCount = Number(match[2]);
    if (shardIndex < 1 || shardIndex > shardCount || shardCount < 1) {
      throw new Error(`Invalid shard selector '${group}'.`);
    }
    selected = selected.filter((_example, index) => index % shardCount === shardIndex - 1);
  }
  return selected;
}

/** Returns deterministic manifest scenarios and rejects incomplete required-case scenario metadata. */
export function getCaseScenarios(example) {
  if (!Array.isArray(example.scenarios) || example.scenarios.length === 0) {
    throw new Error(`${example.id}: phase1_required case must declare at least one deterministic scenario.`);
  }
  for (const scenario of example.scenarios) {
    if (!scenario || typeof scenario.id !== 'string' || scenario.id.trim() === '') {
      throw new Error(`${example.id}: every scenario must have a non-empty id.`);
    }
    if (!Number.isInteger(scenario.frame) || scenario.frame < 0) {
      throw new Error(`${example.id}/${scenario.id}: frame must be a non-negative integer.`);
    }
  }
  return example.scenarios;
}

/** Resolves the pipeline- and shard-specific host executable from explicit profile configuration. */
export async function resolveHostExecutable(profile, pipeline, example) {
  const explicit = profile.hosts?.[pipeline];
  if (explicit && await pathExists(explicit)) {
    return explicit;
  }
  const shardName = example.dslShard;
  if (typeof shardName !== 'string' || shardName.length === 0) {
    throw new Error(`${example.id}: implemented phase1_required case must declare dslShard.`);
  }
  // A manifest shard is a stable generation label, while a sample target may
  // have been split out of an older shared shard during semantic completion.
  // Keep the locked shard candidate first, then resolve the explicit target
  // or the canonical case name so the runner cannot accidentally execute a
  // different example through a stale shared host.
  const caseName = String(example.id ?? '')
    .split(/[^A-Za-z0-9]+/u)
    .filter(Boolean)
    .map((part) => `${part[0].toUpperCase()}${part.slice(1)}`)
    .join('');
  const targetNames = [example.hostTarget, example.dslEntry, caseName]
    .filter((name, index, values) => typeof name === 'string'
      && name.length > 0 && values.indexOf(name) === index);
  const candidates = [
    ...(profile.hostCandidates?.[pipeline] ?? []),
    ...targetNames.flatMap((targetName) => [
      path.join(profile.buildDir, 'gvm_three_samples', 'bin', `${targetName}-${pipeline}`),
      path.join(profile.buildDir, 'bin', `${targetName}-${pipeline}`)
    ]),
    // Preserve compatibility with older manifests whose shard itself is the
    // executable name, but only after trying the semantic case target. This
    // ordering prevents a stale shared Phase1 host from claiming a case that
    // now has a dedicated renderer.
    path.join(profile.buildDir, 'gvm_three_samples', 'bin', `${shardName}-${pipeline}`),
    path.join(profile.buildDir, 'bin', `${shardName}-${pipeline}`)
  ];
  for (const candidate of candidates) {
    if (await pathExists(candidate)) {
      return candidate;
    }
  }
  const examined = [explicit, ...candidates].filter(Boolean);
  throw new Error(`Missing ${pipeline} Three sample host. Examined: ${examined.join(', ') || '<none>'}.`);
}

/** Executes one child process with a hard watchdog and captures bounded diagnostic output. */
export async function executeWithWatchdog(command, args, options) {
  const timeoutMs = options.timeoutMs;
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, {
      cwd: options.cwd,
      env: process.env,
      shell: false,
      stdio: ['ignore', 'pipe', 'pipe']
    });
    let stdout = '';
    let stderr = '';
    let timedOut = false;
    const appendBounded = (previous, chunk) => `${previous}${chunk}`.slice(-64 * 1024);
    child.stdout.on('data', (chunk) => {
      stdout = appendBounded(stdout, chunk.toString('utf8'));
    });
    child.stderr.on('data', (chunk) => {
      stderr = appendBounded(stderr, chunk.toString('utf8'));
    });
    child.on('error', reject);
    const timer = setTimeout(() => {
      timedOut = true;
      child.kill('SIGKILL');
    }, timeoutMs);
    child.on('close', (exitCode, signal) => {
      clearTimeout(timer);
      resolve({ exitCode, signal, timedOut, stdout, stderr });
    });
  });
}

/** Constructs the output artifact paths for one case, scenario, pipeline, backend, and repetition. */
export function makeArtifactPaths(runDir, example, scenario, pipeline, backend, repetition) {
  const artifactDir = path.join(
    runDir,
    'artifacts',
    sanitizeSegment(example.id),
    sanitizeSegment(scenario.id),
    pipeline,
    backend,
    `repeat-${repetition}`
  );
  return {
    artifactDir,
    rgbaPath: path.join(artifactDir, 'final.rgba'),
    metadataPath: path.join(artifactDir, 'final.json'),
    sceneSnapshotPath: path.join(artifactDir, 'scene-snapshot.json'),
    semanticPath: path.join(artifactDir, 'semantic.json'),
    hostLogPath: path.join(artifactDir, 'host.json')
  };
}

/** Builds the explicit host CLI contract for one deterministic Three sample capture. */
export function buildHostArguments(example, scenario, pipeline, backend, assetRoot, artifactPaths) {
  const randomSeed = Number.isInteger(example.randomSeed)
    ? example.randomSeed
    : defaultThreeRandomSeed;
  const args = [
    '--case-id', example.id,
    '--scenario-id', scenario.id,
    '--frame', String(scenario.frame),
    '--random-seed', String(randomSeed),
    '--pipeline', pipeline,
    '--backend', backend,
    '--asset-root', assetRoot,
    '--width', '800',
    '--height', '500',
    '--capture-rgba', artifactPaths.rgbaPath,
    '--capture-metadata', artifactPaths.metadataPath,
    '--scene-snapshot', artifactPaths.sceneSnapshotPath,
    '--semantic-snapshot', artifactPaths.semanticPath
  ];
  if (scenario.inputReplay) {
    args.push('--input-replay', resolveScenarioHostValue(assetRoot, scenario.inputReplay, 'inputReplay'));
  }
  if (scenario.canonicalState) {
    args.push('--canonical-state', resolveScenarioHostValue(assetRoot, scenario.canonicalState, 'canonicalState'));
  }
  return args;
}

/** Returns whether one Loader or Exporter scenario requires a locked semantic sidecar. */
export function scenarioRequiresSemanticSnapshot(scenario) {
  return scenario?.kind === 'loader-snapshot' || scenario?.kind === 'export-round-trip';
}

/** Serializes JSON with recursively sorted object keys for semantic equality checks. */
function serializeCanonicalJson(value) {
  if (Array.isArray(value)) {
    return `[${value.map(serializeCanonicalJson).join(',')}]`;
  }
  if (value && typeof value === 'object') {
    return `{${Object.keys(value).sort().map((key) => (
      `${JSON.stringify(key)}:${serializeCanonicalJson(value[key])}`
    )).join(',')}}`;
  }
  return JSON.stringify(value);
}

/** Validates a Loader canonical scene or Exporter round-trip sidecar against its locked Oracle. */
export function validateSemanticSnapshot(example, scenario, actual, reference) {
  const failures = [];
  if (!scenarioRequiresSemanticSnapshot(scenario)) return failures;
  const expectedFields = new Map([
    ['schemaVersion', 1],
    ['caseId', example.id],
    ['scenarioId', scenario.id],
    ['frame', scenario.frame],
    ['kind', scenario.kind],
    ['canonicalState', scenario.canonicalState ?? null]
  ]);
  for (const [field, expected] of expectedFields) {
    if (actual?.[field] !== expected) {
      failures.push(`${example.id}/${scenario.id}: GVM semantic ${field}=${JSON.stringify(actual?.[field])}; expected ${JSON.stringify(expected)}.`);
    }
    if (reference?.[field] !== expected) {
      failures.push(`${example.id}/${scenario.id}: Oracle semantic ${field}=${JSON.stringify(reference?.[field])}; expected ${JSON.stringify(expected)}.`);
    }
  }
  const allowedFields = [...expectedFields.keys(), 'result'].sort();
  for (const [label, document] of [['GVM', actual], ['Oracle', reference]]) {
    if (!document || typeof document !== 'object' || Array.isArray(document)) {
      failures.push(`${example.id}/${scenario.id}: ${label} semantic sidecar must be an object.`);
      continue;
    }
    const fields = Object.keys(document).sort();
    if (JSON.stringify(fields) !== JSON.stringify(allowedFields)) {
      failures.push(`${example.id}/${scenario.id}: ${label} semantic sidecar fields must exactly match ${allowedFields.join(', ')}.`);
    }
    if (!document.result || typeof document.result !== 'object' || Array.isArray(document.result)) {
      failures.push(`${example.id}/${scenario.id}: ${label} semantic result must be an object.`);
    }
  }
  if (actual?.result && reference?.result
    && serializeCanonicalJson(actual.result) !== serializeCanonicalJson(reference.result)) {
    failures.push(`${example.id}/${scenario.id}: GVM semantic result differs from the locked Oracle result.`);
  }
  const sha256Pattern = /^[a-f0-9]{64}$/u;
  if (scenario.kind === 'loader-snapshot') {
    const expectedRenderableCount = Number.isInteger(example.loaderRenderableObjectCount)
      ? example.loaderRenderableObjectCount
      : example.renderableObjectCount;
    if (actual?.result?.renderableObjectCount !== expectedRenderableCount) {
      failures.push(`${example.id}/${scenario.id}: Loader semantic renderableObjectCount must equal ${expectedRenderableCount}.`);
    }
    if (actual?.result?.sceneRootCount !== (example.sceneRoots ?? []).length) {
      failures.push(`${example.id}/${scenario.id}: Loader semantic sceneRootCount must equal the manifest Scene root count.`);
    }
    if (!sha256Pattern.test(actual?.result?.canonicalSceneSha256 ?? '')) {
      failures.push(`${example.id}/${scenario.id}: Loader semantic result must include canonicalSceneSha256.`);
    }
  } else {
    for (const field of ['canonicalOutputSha256', 'sourceSemanticSha256', 'reimportedSemanticSha256']) {
      if (!sha256Pattern.test(actual?.result?.[field] ?? '')) {
        failures.push(`${example.id}/${scenario.id}: Exporter semantic result must include ${field}.`);
      }
    }
    if (actual?.result?.roundTripEquivalent !== true
      || actual?.result?.sourceSemanticSha256 !== actual?.result?.reimportedSemanticSha256) {
      failures.push(`${example.id}/${scenario.id}: Exporter canonical output must round-trip to the exact source semantic hash.`);
    }
  }
  return failures;
}

/** Resolves file-like scenario values inside the locked asset pack while preserving symbolic labels. */
function resolveScenarioHostValue(assetRoot, value, label) {
  const text = String(value).trim();
  if (!text.includes('/') && path.extname(text) === '') return text;
  if (path.isAbsolute(text)) {
    throw new Error(`${label} must be relative to the locked asset pack: '${text}'.`);
  }
  const normalized = path.normalize(text);
  if (normalized === '..' || normalized.startsWith(`..${path.sep}`)) {
    throw new Error(`${label} escapes the locked asset pack: '${text}'.`);
  }
  return path.resolve(assetRoot, normalized);
}

/** Resolves the exact logical-renderable total locked for one deterministic scenario. */
function expectedScenarioRenderableObjectCount(example, scenario) {
  return Number.isInteger(scenario?.renderableObjectCount)
    ? scenario.renderableObjectCount
    : example.renderableObjectCount;
}

/** Resolves how many times one generated Scene RenderClass must execute in a scenario. */
function expectedScenePassInvocationCount(scenario, sceneRoot, scenePass) {
  const override = (scenario?.scenePassInvocations ?? []).find((candidate) => (
    candidate.sceneRoot === sceneRoot && candidate.scenePass === scenePass
  ));
  return override?.invocationCount ?? 1;
}

/** Validates the runtime RenderSet snapshot required for a complex logical Three scene. */
export function validateRenderSetSnapshot(example, snapshot, scenario = null) {
  const failures = [];
  if (example.renderSetPolicy !== 'required') {
    return failures;
  }
  if (!Array.isArray(snapshot.sceneRoots)) {
    return [`${example.id}: runtime snapshot does not contain sceneRoots.`];
  }
  const expectedRootNames = (example.sceneRoots ?? []).map((root) => (
    typeof root === 'string' ? root : root.name
  ));
  if (snapshot.sceneRoots.length !== expectedRootNames.length) {
    failures.push(`${example.id}: runtime scene root count ${snapshot.sceneRoots.length} differs from manifest ${expectedRootNames.length}.`);
  }
  const runtimeRootNames = snapshot.sceneRoots.map((root) => root.id);
  if (runtimeRootNames.some((rootName) => typeof rootName !== 'string' || rootName.length === 0)) {
    failures.push(`${example.id}: every runtime Scene root must report its manifest id.`);
  } else {
    const uniqueRuntimeRootNames = new Set(runtimeRootNames);
    if (uniqueRuntimeRootNames.size !== runtimeRootNames.length) {
      failures.push(`${example.id}: runtime Scene root ids must be unique.`);
    }
    for (const expectedRootName of expectedRootNames) {
      if (!uniqueRuntimeRootNames.has(expectedRootName)) {
        failures.push(`${example.id}: runtime snapshot is missing Scene root '${expectedRootName}'.`);
      }
    }
  }
  const expectedComponentSchema = (example.componentSchema ?? []).map((component) => (
    typeof component === 'string'
      ? { name: component, kind: null, role: null }
      : { name: component.name, kind: component.kind, role: component.role }
  ));
  const allLogicalRenderableIds = [];
  let totalRenderableCount = 0;
  for (const root of snapshot.sceneRoots) {
    const manifestRoot = (example.sceneRoots ?? []).find((candidate) => (
      (typeof candidate === 'string' ? candidate : candidate.name) === root.id
    ));
    const expectedRenderSetCount = manifestRoot?.renderSetRuntimeInstanceCount ?? 1;
    if (root.renderSetCount !== expectedRenderSetCount) {
      failures.push(expectedRenderSetCount === 1
        ? `${example.id}/${root.id ?? '<unknown>'}: complex Scene must own exactly one RenderSet; observed ${root.renderSetCount}.`
        : `${example.id}/${root.id ?? '<unknown>'}: simple Scene must own zero RenderSets; observed ${root.renderSetCount}.`);
    }
    if (!Number.isInteger(root.renderableObjectCount) || root.renderableObjectCount < 0) {
      failures.push(`${example.id}/${root.id ?? '<unknown>'}: root renderableObjectCount must be a non-negative integer.`);
    } else {
      totalRenderableCount += root.renderableObjectCount;
    }
    if (expectedRenderSetCount === 0) {
      if (root.renderSetId != null || root.renderSetType != null
        || root.entityCount != null && root.entityCount !== 0
        || Array.isArray(root.entities) && root.entities.length !== 0) {
        failures.push(`${example.id}/${root.id ?? '<unknown>'}: simple Scene root must not expose RenderSet entity metadata.`);
      }
      continue;
    }
    if (typeof root.renderSetId !== 'string' || root.renderSetId.length === 0) {
      failures.push(`${example.id}/${root.id ?? '<unknown>'}: runtime snapshot must expose a stable renderSetId.`);
    }
    if (manifestRoot && root.renderSetType !== manifestRoot.renderSetType) {
      failures.push(`${example.id}/${root.id ?? '<unknown>'}: runtime RenderSet type differs from the manifest.`);
    }
    if (!Number.isInteger(root.entityCount) || root.entityCount < 1) {
      failures.push(`${example.id}/${root.id ?? '<unknown>'}: RenderSet entityCount must be positive.`);
    } else {
      if (root.renderableObjectCount !== root.entityCount) {
        failures.push(`${example.id}/${root.id ?? '<unknown>'}: RenderSet entityCount must equal the root renderableObjectCount.`);
      }
    }
    if (!Array.isArray(root.entities) || root.entities.length !== root.entityCount) {
      failures.push(`${example.id}/${root.id ?? '<unknown>'}: entity metadata does not match entityCount.`);
    } else {
      const entityIds = new Set();
      const rootLogicalIds = new Set();
      for (const entity of root.entities) {
        if ((typeof entity.entityId !== 'string' || entity.entityId.length === 0)
          && (!Number.isInteger(entity.entityId) || entity.entityId < 0)) {
          failures.push(`${example.id}/${root.id ?? '<unknown>'}: every entity must report a stable entityId.`);
        } else if (entityIds.has(entity.entityId)) {
          failures.push(`${example.id}/${root.id ?? '<unknown>'}: entityId values must be unique.`);
        } else {
          entityIds.add(entity.entityId);
        }
        if (typeof entity.logicalRenderableId !== 'string' || entity.logicalRenderableId.length === 0) {
          failures.push(`${example.id}/${root.id ?? '<unknown>'}: every entity must identify exactly one logical renderable.`);
        } else if (rootLogicalIds.has(entity.logicalRenderableId)) {
          failures.push(`${example.id}/${root.id ?? '<unknown>'}: logical renderables must map one-to-one to entities.`);
        } else {
          rootLogicalIds.add(entity.logicalRenderableId);
          allLogicalRenderableIds.push(entity.logicalRenderableId);
        }
        if (!Number.isInteger(entity.instanceCount) || entity.instanceCount < 1) {
          failures.push(`${example.id}/${root.id ?? '<unknown>'}: every entity must report a positive instanceCount.`);
        }
      }
    }
    if (!Array.isArray(root.componentSchema) || root.componentSchema.length === 0) {
      failures.push(`${example.id}/${root.id ?? '<unknown>'}: componentSchema is missing.`);
    } else if (expectedComponentSchema.length > 0 && JSON.stringify(root.componentSchema) !== JSON.stringify(expectedComponentSchema)) {
      failures.push(`${example.id}/${root.id ?? '<unknown>'}: runtime componentSchema differs from the manifest ABI.`);
    }
    const expectedPasses = (example.scenePasses ?? []).filter((scenePass) => scenePass.sceneRoot === root.id);
    const expectedDrawCommandCount = expectedPasses.reduce((total, scenePass) => (
      total + expectedScenePassInvocationCount(scenario, root.id, scenePass.name)
    ), 0);
    if (root.drawCommandCount !== expectedDrawCommandCount) {
      failures.push(`${example.id}/${root.id ?? '<unknown>'}: automatic RenderSet draw count ${JSON.stringify(root.drawCommandCount)} differs from ${expectedDrawCommandCount} scenario pass invocations.`);
    }
    if (root.directDrawFallback !== false) {
      failures.push(`${example.id}/${root.id ?? '<unknown>'}: direct-draw fallback flag must be explicitly false.`);
    }
    if (!Array.isArray(root.scenePasses) || root.scenePasses.length !== expectedPasses.length) {
      failures.push(`${example.id}/${root.id ?? '<unknown>'}: runtime scene pass records must exactly match the manifest.`);
    } else {
      const runtimePasses = new Map();
      for (const runtimePass of root.scenePasses) {
        if (runtimePasses.has(runtimePass.name)) {
          failures.push(`${example.id}/${root.id ?? '<unknown>'}: runtime scene pass names must be unique.`);
        }
        runtimePasses.set(runtimePass.name, runtimePass);
      }
      for (const expectedPass of expectedPasses) {
        const runtimePass = runtimePasses.get(expectedPass.name);
        const expectedInvocationCount = expectedScenePassInvocationCount(scenario, root.id, expectedPass.name);
        if (!runtimePass) {
          failures.push(`${example.id}/${root.id ?? '<unknown>'}: runtime snapshot is missing scene pass '${expectedPass.name}'.`);
          continue;
        }
        if (runtimePass.renderClass !== expectedPass.renderClass) {
          failures.push(`${example.id}/${root.id ?? '<unknown>'}/${expectedPass.name}: generated RenderClass identity differs from the manifest.`);
        }
        if (runtimePass.renderSetId !== root.renderSetId) {
          failures.push(`${example.id}/${root.id ?? '<unknown>'}/${expectedPass.name}: every scene pass must reuse the Scene's unique RenderSet instance.`);
        }
        if (runtimePass.renderSetBindingCount !== 1
          || runtimePass.drawMode !== 'render-set-indexed-indirect'
          || runtimePass.invocationCount !== expectedInvocationCount
          || runtimePass.drawCommandCount !== expectedInvocationCount
          || runtimePass.usesStandaloneGeometry !== false
          || runtimePass.usesExplicitDrawCount !== false) {
          failures.push(`${example.id}/${root.id ?? '<unknown>'}/${expectedPass.name}: runtime pass did not execute the scenario's ${expectedInvocationCount} RenderSet-only indexed-indirect draw invocation(s).`);
        }
      }
    }
  }
  if (Array.isArray(scenario?.scenePassSequence)) {
    if (!Array.isArray(snapshot.scenePassSequence)) {
      failures.push(`${example.id}/${scenario.id}: runtime snapshot is missing the locked ordered Scene pass sequence.`);
    } else if (snapshot.scenePassSequence.length !== scenario.scenePassSequence.length) {
      failures.push(`${example.id}/${scenario.id}: runtime ordered Scene pass sequence length ${snapshot.scenePassSequence.length} differs from manifest ${scenario.scenePassSequence.length}.`);
    } else {
      for (let sequenceIndex = 0; sequenceIndex < scenario.scenePassSequence.length; sequenceIndex += 1) {
        const expectedEntry = scenario.scenePassSequence[sequenceIndex];
        const runtimeEntry = snapshot.scenePassSequence[sequenceIndex];
        if (runtimeEntry?.sceneRoot !== expectedEntry.sceneRoot
          || runtimeEntry?.scenePass !== expectedEntry.scenePass
          || runtimeEntry?.entityOrdinal !== expectedEntry.entityOrdinal) {
          failures.push(`${example.id}/${scenario.id}: runtime ordered Scene pass sequence differs at index ${sequenceIndex}.`);
        }
      }
    }
  }
  const expectedRenderableObjectCount = expectedScenarioRenderableObjectCount(example, scenario);
  if (Number.isInteger(expectedRenderableObjectCount) && totalRenderableCount !== expectedRenderableObjectCount) {
    failures.push(`${example.id}: runtime renderable total ${totalRenderableCount} differs from scenario renderableObjectCount ${expectedRenderableObjectCount}.`);
  }
  if (new Set(allLogicalRenderableIds).size !== allLogicalRenderableIds.length) {
    failures.push(`${example.id}: logicalRenderableId values must be unique across Scene roots.`);
  }
  const expectedContainsInstancing = typeof scenario?.containsInstancing === 'boolean'
    ? scenario.containsInstancing
    : example.containsInstancing;
  if (expectedContainsInstancing) {
    const hasInstancedEntity = snapshot.sceneRoots.some((root) => (
      Array.isArray(root.entities) && root.entities.some((entity) => entity.instanceCount > 1)
    ));
    if (!hasInstancedEntity) {
      failures.push(`${example.id}: manifest declares instancing but runtime snapshot has no instanced entity.`);
    }
  } else if (snapshot.sceneRoots.some((root) => (
    Array.isArray(root.entities) && root.entities.some((entity) => entity.instanceCount > 1)
  ))) {
    failures.push(`${example.id}: runtime snapshot contains instancing that is absent from the manifest.`);
  }
  return failures;
}

/** Validates deterministic capture identity and the common structural scene snapshot contract. */
export function validateStructuralSnapshot(example, scenario, snapshot) {
  if (!snapshot || typeof snapshot !== 'object' || Array.isArray(snapshot)) {
    return [`${example.id}/${scenario.id}: structural snapshot must be a JSON object.`];
  }
  const failures = [];
  const expectedRenderableObjectCount = expectedScenarioRenderableObjectCount(example, scenario);
  for (const [field, expected] of [
    ['caseId', example.id],
    ['scenarioId', scenario.id],
    ['frame', scenario.frame],
    ['renderableObjectCount', expectedRenderableObjectCount],
    ['gpuWorkDslOnly', true]
  ]) {
    if (snapshot[field] !== expected) {
      failures.push(`${example.id}/${scenario.id}: snapshot ${field}=${JSON.stringify(snapshot[field])}; expected ${JSON.stringify(expected)}.`);
    }
  }
  const expectedRenderSetCount = (example.sceneRoots ?? []).reduce((sum, sceneRoot) => (
    sum + Number(sceneRoot?.renderSetRuntimeInstanceCount ?? 0)
  ), 0);
  if (snapshot.sceneRenderSetCount !== expectedRenderSetCount) {
    failures.push(`${example.id}/${scenario.id}: snapshot sceneRenderSetCount=${JSON.stringify(snapshot.sceneRenderSetCount)}; expected ${expectedRenderSetCount}.`);
  }
  if (snapshot.renderSetPolicy != null && snapshot.renderSetPolicy !== example.renderSetPolicy) {
    failures.push(`${example.id}/${scenario.id}: snapshot renderSetPolicy differs from the manifest.`);
  }
  if (!Number.isInteger(snapshot.drawCommandCount) || snapshot.drawCommandCount < 1) {
    failures.push(`${example.id}/${scenario.id}: snapshot drawCommandCount must be a positive integer.`);
  }
  if (example.renderSetPolicy === 'not-required' && (example.scenePasses ?? []).length > 0) {
    const expectedScenePassCount = example.scenePasses.reduce((total, scenePass) => (
      total + expectedScenePassInvocationCount(scenario, scenePass.sceneRoot, scenePass.name)
    ), 0);
    if (snapshot.scenePassCount !== expectedScenePassCount) {
      failures.push(`${example.id}/${scenario.id}: snapshot scenePassCount=${JSON.stringify(snapshot.scenePassCount)}; expected ${expectedScenePassCount}.`);
    }
    if (Array.isArray(scenario.scenePassSequence)) {
      if (!Array.isArray(snapshot.scenePassSequence)) {
        failures.push(`${example.id}/${scenario.id}: runtime snapshot is missing the locked ordered Scene pass sequence.`);
      } else if (snapshot.scenePassSequence.length !== scenario.scenePassSequence.length) {
        failures.push(`${example.id}/${scenario.id}: runtime ordered Scene pass sequence length ${snapshot.scenePassSequence.length} differs from manifest ${scenario.scenePassSequence.length}.`);
      } else {
        for (let sequenceIndex = 0; sequenceIndex < scenario.scenePassSequence.length; sequenceIndex += 1) {
          const expectedEntry = scenario.scenePassSequence[sequenceIndex];
          const runtimeEntry = snapshot.scenePassSequence[sequenceIndex];
          if (runtimeEntry?.sceneRoot !== expectedEntry.sceneRoot
            || runtimeEntry?.scenePass !== expectedEntry.scenePass
            || runtimeEntry?.entityOrdinal !== expectedEntry.entityOrdinal) {
            failures.push(`${example.id}/${scenario.id}: runtime ordered Scene pass sequence differs at index ${sequenceIndex}.`);
          }
        }
      }
    }
  }
  if (example.renderSetPolicy === 'not-required'
    && example.containsInstancing === false
    && expectedRenderableObjectCount > 0
    && snapshot.instanceCount !== 1) {
    failures.push(`${example.id}/${scenario.id}: non-instanced snapshot must report instanceCount=1.`);
  }
  return failures;
}

/** Returns the canonical oracle paths for one case and deterministic scenario. */
export function makeOraclePaths(oracleRoot, example, scenario) {
  const basePath = path.join(oracleRoot, sanitizeSegment(example.id), sanitizeSegment(scenario.id));
  return {
    rgbaPath: `${basePath}.rgba`,
    metadataPath: `${basePath}.json`,
    semanticPath: `${basePath}.semantic.json`
  };
}

/** Validates the fixed identity, frame, extent, format, and random stream of one capture pair. */
export function validateDeterministicCaptureMetadata(
  example,
  scenario,
  actualMetadata,
  referenceMetadata,
  expectedPipeline = null,
  expectedBackend = null
) {
  const failures = [];
  const randomSeed = Number.isInteger(example.randomSeed)
    ? example.randomSeed
    : defaultThreeRandomSeed;
  const expectedFields = new Map([
    ['caseId', example.id],
    ['scenarioId', scenario.id],
    ['frame', scenario.frame],
    ['randomSeed', randomSeed],
    ['width', 800],
    ['height', 500],
    ['rowStrideBytes', 3200],
    ['byteCount', 1_600_000],
    ['format', 'rgba8unorm']
  ]);
  for (const [field, expected] of expectedFields) {
    if (actualMetadata?.[field] !== expected) {
      failures.push(`${example.id}/${scenario.id}: GVM metadata ${field}=${JSON.stringify(actualMetadata?.[field])}; expected ${JSON.stringify(expected)}.`);
    }
    if (referenceMetadata?.[field] !== expected) {
      failures.push(`${example.id}/${scenario.id}: Three Oracle metadata ${field}=${JSON.stringify(referenceMetadata?.[field])}; expected ${JSON.stringify(expected)}.`);
    }
  }
  if (referenceMetadata?.samplePolicy?.mode !== 'single-sample'
    || referenceMetadata?.samplePolicy?.msaaEnabled !== false
    || referenceMetadata?.samplePolicy?.simulateMsaa !== false) {
    failures.push(`${example.id}/${scenario.id}: Three Oracle metadata must prove single-sample capture without MSAA simulation.`);
  }
  for (const [field, expected] of [['pipeline', expectedPipeline], ['backend', expectedBackend]]) {
    if (expected != null && actualMetadata?.[field] !== expected) {
      failures.push(`${example.id}/${scenario.id}: GVM metadata ${field}=${JSON.stringify(actualMetadata?.[field])}; expected ${JSON.stringify(expected)}.`);
    }
  }
  if (scenario.inputReplay) {
    const replayFields = ['sha256', 'caseId', 'scenarioId', 'captureFrame', 'eventCount', 'target'];
    for (const field of replayFields) {
      const actualValue = actualMetadata?.inputReplay?.[field];
      const referenceValue = referenceMetadata?.inputReplay?.[field];
      if (referenceValue == null) {
        failures.push(`${example.id}/${scenario.id}: Three Oracle metadata is missing inputReplay.${field}.`);
      } else if (actualValue !== referenceValue) {
        failures.push(`${example.id}/${scenario.id}: GVM inputReplay.${field}=${JSON.stringify(actualValue)}; expected Oracle value ${JSON.stringify(referenceValue)}.`);
      }
    }
  } else {
    if (actualMetadata?.inputReplay != null) {
      failures.push(`${example.id}/${scenario.id}: GVM metadata declares an input replay for a scenario that has none.`);
    }
    if (referenceMetadata?.inputReplay != null) {
      failures.push(`${example.id}/${scenario.id}: Three Oracle metadata declares an input replay for a scenario that has none.`);
    }
  }
  return failures;
}

/** Compares one host capture to the immutable Three oracle and validates the RenderSet runtime contract. */
export async function validateCapture(example, scenario, pipeline, backend, artifactPaths, oracleRoot) {
  const oraclePaths = makeOraclePaths(oracleRoot, example, scenario);
  const [actual, reference, snapshot] = await Promise.all([
    loadRgbaArtifact(artifactPaths.rgbaPath, artifactPaths.metadataPath),
    loadRgbaArtifact(oraclePaths.rgbaPath, oraclePaths.metadataPath),
    loadJson(artifactPaths.sceneSnapshotPath)
  ]);
  const imageComparison = compareThreeCaptures(reference, actual);
  const metadataFailures = validateDeterministicCaptureMetadata(
    example,
    scenario,
    actual.metadata,
    reference.metadata,
    pipeline,
    backend
  );
  const renderSetFailures = validateRenderSetSnapshot(example, snapshot, scenario);
  const structuralFailures = validateStructuralSnapshot(example, scenario, snapshot);
  let semantic = null;
  let semanticFailures = [];
  if (scenarioRequiresSemanticSnapshot(scenario)) {
    const [actualSemantic, referenceSemantic] = await Promise.all([
      loadJson(artifactPaths.semanticPath),
      loadJson(oraclePaths.semanticPath)
    ]);
    semantic = { actual: actualSemantic, reference: referenceSemantic };
    semanticFailures = validateSemanticSnapshot(example, scenario, actualSemantic, referenceSemantic);
  }
  return {
    status: imageComparison.failures.length === 0
      && metadataFailures.length === 0
      && structuralFailures.length === 0
      && renderSetFailures.length === 0
      && semanticFailures.length === 0
      ? 'pass'
      : 'fail',
    metrics: imageComparison.metrics,
    failures: [
      ...metadataFailures,
      ...imageComparison.failures,
      ...structuralFailures,
      ...renderSetFailures,
      ...semanticFailures
    ],
    oraclePaths,
    snapshot,
    semantic
  };
}

/** Runs one required case quadrant and always returns a report record instead of an execution-time skip. */
export async function runQuadrant(context, example, scenario, pipeline, backend, repetition) {
  const artifactPaths = makeArtifactPaths(context.runDir, example, scenario, pipeline, backend, repetition);
  await fs.mkdir(artifactPaths.artifactDir, { recursive: true });
  const startedAt = new Date();
  const result = {
    caseId: example.id,
    scenarioId: scenario.id,
    pipeline,
    backend,
    repetition,
    status: 'fail',
    startedAt: startedAt.toISOString(),
    durationMs: 0,
    artifacts: artifactPaths,
    failures: []
  };
  try {
    const host = await resolveHostExecutable(context.profile, pipeline, example);
    const args = buildHostArguments(example, scenario, pipeline, backend, context.assetRoot, artifactPaths);
    const execution = await executeWithWatchdog(host, args, {
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
      const validation = await validateCapture(
        example,
        scenario,
        pipeline,
        backend,
        artifactPaths,
        context.oracleRoot
      );
      result.validation = validation;
      result.failures.push(...validation.failures);
    }
  } catch (error) {
    result.failures.push(error instanceof Error ? error.message : String(error));
  }
  result.status = result.failures.length === 0 ? 'pass' : 'fail';
  result.durationMs = Date.now() - startedAt.getTime();
  return result;
}

/** Compares two successful GVM quadrant captures using the same immutable image thresholds. */
export async function compareQuadrantPair(left, right, relation) {
  const comparison = {
    relation,
    caseId: left.caseId,
    scenarioId: left.scenarioId,
    repetition: left.repetition,
    left: { pipeline: left.pipeline, backend: left.backend },
    right: { pipeline: right.pipeline, backend: right.backend },
    status: 'fail',
    failures: []
  };
  if (left.status !== 'pass' || right.status !== 'pass') {
    comparison.failures.push('Cross-quadrant comparison requires both oracle comparisons to pass.');
    return comparison;
  }
  try {
    const [leftImage, rightImage] = await Promise.all([
      loadRgbaArtifact(left.artifacts.rgbaPath, left.artifacts.metadataPath),
      loadRgbaArtifact(right.artifacts.rgbaPath, right.artifacts.metadataPath)
    ]);
    const result = compareThreeCaptures(leftImage, rightImage);
    comparison.metrics = result.metrics;
    comparison.failures = result.failures;
    comparison.status = result.failures.length === 0 ? 'pass' : 'fail';
  } catch (error) {
    comparison.failures.push(error instanceof Error ? error.message : String(error));
  }
  return comparison;
}

/** Builds all required same-backend and same-pipeline cross-quadrant comparisons. */
export async function buildCrossComparisons(quadrants) {
  const byKey = new Map(quadrants.map((entry) => [
    `${entry.caseId}|${entry.scenarioId}|${entry.repetition}|${entry.pipeline}|${entry.backend}`,
    entry
  ]));
  const comparisonDefinitions = [
    ['legacy', 'metal', 'uglir', 'metal', 'pipeline-parity-metal'],
    ['legacy', 'vulkan', 'uglir', 'vulkan', 'pipeline-parity-vulkan'],
    ['legacy', 'metal', 'legacy', 'vulkan', 'backend-parity-legacy'],
    ['uglir', 'metal', 'uglir', 'vulkan', 'backend-parity-experimental']
  ];
  const identities = [...new Set(quadrants.map((entry) => (
    `${entry.caseId}|${entry.scenarioId}|${entry.repetition}`
  )))];
  const comparisons = [];
  for (const identity of identities) {
    for (const [leftPipeline, leftBackend, rightPipeline, rightBackend, relation] of comparisonDefinitions) {
      const left = byKey.get(`${identity}|${leftPipeline}|${leftBackend}`);
      const right = byKey.get(`${identity}|${rightPipeline}|${rightBackend}`);
      if (left && right) {
        comparisons.push(await compareQuadrantPair(left, right, relation));
      }
    }
  }
  return comparisons;
}

/** Requires byte-exact output stability between the first and one later repetition. */
export async function compareRepetitionPair(baseline, candidate) {
  const comparison = {
    relation: 'repeat-stability',
    caseId: baseline.caseId,
    scenarioId: baseline.scenarioId,
    pipeline: baseline.pipeline,
    backend: baseline.backend,
    baselineRepetition: baseline.repetition,
    candidateRepetition: candidate.repetition,
    status: 'fail',
    differingBytes: null,
    failures: []
  };
  if (baseline.status !== 'pass' || candidate.status !== 'pass') {
    comparison.failures.push('Repeat stability requires both quadrant captures to pass.');
    return comparison;
  }
  try {
    const [baselineImage, candidateImage] = await Promise.all([
      loadRgbaArtifact(baseline.artifacts.rgbaPath, baseline.artifacts.metadataPath),
      loadRgbaArtifact(candidate.artifacts.rgbaPath, candidate.artifacts.metadataPath)
    ]);
    if (baselineImage.width !== candidateImage.width || baselineImage.height !== candidateImage.height) {
      comparison.failures.push(
        `Repeated deterministic capture dimensions differ: baseline=${baselineImage.width}x${baselineImage.height}, candidate=${candidateImage.width}x${candidateImage.height}.`
      );
      comparison.status = 'fail';
      return comparison;
    }
    let differingBytes = 0;
    for (let byteIndex = 0; byteIndex < baselineImage.pixels.length; byteIndex += 1) {
      if (baselineImage.pixels[byteIndex] !== candidateImage.pixels[byteIndex]) differingBytes += 1;
    }
    comparison.differingBytes = differingBytes;
    if (differingBytes > 0) {
      comparison.failures.push(`Repeated deterministic capture differs in ${differingBytes} RGBA bytes.`);
    }
    if (baseline.artifacts.semanticPath && candidate.artifacts.semanticPath
      && await pathExists(baseline.artifacts.semanticPath)
      && await pathExists(candidate.artifacts.semanticPath)) {
      const [baselineSemantic, candidateSemantic] = await Promise.all([
        loadJson(baseline.artifacts.semanticPath),
        loadJson(candidate.artifacts.semanticPath)
      ]);
      if (serializeCanonicalJson(baselineSemantic) !== serializeCanonicalJson(candidateSemantic)) {
        comparison.failures.push('Repeated deterministic semantic snapshot differs.');
      }
    }
  } catch (error) {
    comparison.failures.push(error instanceof Error ? error.message : String(error));
  }
  comparison.status = comparison.failures.length === 0 ? 'pass' : 'fail';
  return comparison;
}

/** Builds exact repeat-stability comparisons for every selected case quadrant. */
export async function buildStabilityComparisons(quadrants, repetitions) {
  if (repetitions < 2) return [];
  const groups = new Map();
  for (const quadrant of quadrants) {
    const key = `${quadrant.caseId}|${quadrant.scenarioId}|${quadrant.pipeline}|${quadrant.backend}`;
    const group = groups.get(key) ?? [];
    group.push(quadrant);
    groups.set(key, group);
  }
  const comparisons = [];
  for (const group of groups.values()) {
    group.sort((left, right) => left.repetition - right.repetition);
    const baseline = group.find((entry) => entry.repetition === 1);
    if (!baseline) continue;
    for (let repetition = 2; repetition <= repetitions; repetition += 1) {
      const candidate = group.find((entry) => entry.repetition === repetition);
      if (candidate) comparisons.push(await compareRepetitionPair(baseline, candidate));
    }
  }
  return comparisons;
}

/** Calculates gate and real-coverage accounting without counting deferred cases as passes. */
export function calculateCoverage(
  manifest,
  selectedCases,
  quadrants,
  crossComparisons,
  selectedPipelines,
  selectedBackends,
  expectedRepetitions = 1,
  stabilityComparisons = []
) {
  const requiredTotal = manifest.examples.filter((example) => example.status === 'phase1_required').length;
  const deferredTotal = manifest.examples.filter((example) => example.status === 'deferred_missing_capability').length;
  const excludedTotal = manifest.examples.filter((example) => example.status === 'excluded_upstream').length;
  const pendingTotal = manifest.examples.filter((example) => example.status === 'audit_pending').length;
  const matrixComplete = requiredPipelines.every((pipeline) => selectedPipelines.includes(pipeline))
    && requiredBackends.every((backend) => selectedBackends.includes(backend))
    && selectedPipelines.length === requiredPipelines.length
    && selectedBackends.length === requiredBackends.length;
  const selectionComplete = selectedCases.length === requiredTotal;
  const expectedQuadrantsPerScenario = requiredPipelines.length * requiredBackends.length * expectedRepetitions;
  const expectedComparisonsPerScenario = 4 * expectedRepetitions;
  const expectedStabilityComparisonsPerScenario = selectedPipelines.length
    * selectedBackends.length
    * Math.max(expectedRepetitions - 1, 0);
  const diagnosticPassingCases = selectedCases.filter((example) => {
    const scenarios = getCaseScenarios(example);
    return scenarios.every((scenario) => {
      const caseQuadrants = quadrants.filter((entry) => entry.caseId === example.id && entry.scenarioId === scenario.id);
      const quadrantKeys = new Set(caseQuadrants.map((entry) => (
        `${entry.repetition}|${entry.pipeline}|${entry.backend}`
      )));
      const caseComparisons = crossComparisons.filter((entry) => (
        entry.caseId === example.id && entry.scenarioId === scenario.id
      ));
      const comparisonKeys = new Set(caseComparisons.map((entry) => (
        `${entry.repetition}|${entry.relation}`
      )));
      const caseStabilityComparisons = stabilityComparisons.filter((entry) => (
        entry.caseId === example.id && entry.scenarioId === scenario.id
      ));
      const stabilityKeys = new Set(caseStabilityComparisons.map((entry) => (
        `${entry.pipeline}|${entry.backend}|${entry.candidateRepetition}`
      )));
      return matrixComplete
        && caseQuadrants.length === expectedQuadrantsPerScenario
        && quadrantKeys.size === expectedQuadrantsPerScenario
        && caseQuadrants.every((entry) => entry.status === 'pass')
        && caseComparisons.length === expectedComparisonsPerScenario
        && comparisonKeys.size === expectedComparisonsPerScenario
        && caseComparisons.every((entry) => entry.status === 'pass')
        && caseStabilityComparisons.length === expectedStabilityComparisonsPerScenario
        && stabilityKeys.size === expectedStabilityComparisonsPerScenario
        && caseStabilityComparisons.every((entry) => entry.status === 'pass');
    });
  }).length;
  const passingCases = matrixComplete && selectionComplete ? diagnosticPassingCases : 0;
  const gateComplete = pendingTotal === 0
    && matrixComplete
    && selectionComplete
    && passingCases === requiredTotal;
  return {
    manifestEquation: `588 = ${excludedTotal} excluded_upstream + ${deferredTotal} deferred_missing_capability + ${requiredTotal} phase1_required + ${pendingTotal} audit_pending`,
    excludedTotal,
    deferredTotal,
    requiredTotal,
    pendingTotal,
    selectedRequired: selectedCases.length,
    matrixComplete,
    selectionComplete,
    gateComplete,
    selectedPipelines: [...selectedPipelines],
    selectedBackends: [...selectedBackends],
    diagnosticPassingRequired: diagnosticPassingCases,
    passingRequired: passingCases,
    gatePassRate: requiredTotal === 0 ? 0 : passingCases / requiredTotal,
    realCoverageRate: passingCases / 511,
    thresholds: comparisonThresholds
  };
}

/** Runs the selected deterministic cases through every requested pipeline/backend quadrant. */
export async function runThreeMatrix(context) {
  const selectedCases = selectCases(context.manifest, context.status, context.group);
  if (selectedCases.length === 0) {
    throw new Error(`No examples match status '${normalizeStatus(context.status)}' and group '${context.group}'.`);
  }
  const quadrants = [];
  for (let repetition = 1; repetition <= context.repetitions; repetition += 1) {
    for (const example of selectedCases) {
      for (const scenario of getCaseScenarios(example)) {
        for (const pipeline of context.pipelines) {
          for (const backend of context.backends) {
            const result = await runQuadrant(context, example, scenario, pipeline, backend, repetition);
            quadrants.push(result);
            context.onProgress?.(result, quadrants.length);
          }
        }
      }
    }
  }
  const crossComparisons = context.pipelines.length === 2 && context.backends.length === 2
    ? await buildCrossComparisons(quadrants)
    : [];
  const stabilityComparisons = await buildStabilityComparisons(quadrants, context.repetitions);
  const coverage = calculateCoverage(
    context.manifest,
    selectedCases,
    quadrants,
    crossComparisons,
    context.pipelines,
    context.backends,
    context.repetitions,
    stabilityComparisons
  );
  // Preserve per-case cardinalities in a single-case diagnostic report so
  // the report generator can verify a complete strict closure without
  // mistaking the selected shard for a full-manifest gate.
  if (selectedCases.length === 1) {
    const selectedCaseId = selectedCases[0].id;
    const caseQuadrants = quadrants.filter((entry) => entry.caseId === selectedCaseId);
    const caseComparisons = crossComparisons.filter((entry) => entry.caseId === selectedCaseId);
    const caseStabilityComparisons = stabilityComparisons.filter((entry) => entry.caseId === selectedCaseId);
    coverage.caseId = selectedCaseId;
    coverage.scenarioCount = getCaseScenarios(selectedCases[0]).length;
    coverage.quadrantCount = caseQuadrants.length;
    coverage.crossComparisonCount = caseComparisons.length;
    coverage.stabilityComparisonCount = caseStabilityComparisons.length;
    coverage.oracleFailures = caseQuadrants.reduce(
      (count, entry) => count + (entry.status === 'pass' ? 0 : 1), 0);
    coverage.crossFailures = caseComparisons.reduce(
      (count, entry) => count + (entry.status === 'pass' ? 0 : 1), 0);
    coverage.stabilityFailures = caseStabilityComparisons.reduce(
      (count, entry) => count + (entry.status === 'pass' ? 0 : 1), 0);
  }
  const diagnosticsPass = quadrants.every((entry) => entry.status === 'pass')
    && crossComparisons.every((entry) => entry.status === 'pass')
    && stabilityComparisons.every((entry) => entry.status === 'pass');
  const status = !diagnosticsPass
    ? 'fail'
    : coverage.gateComplete
      ? 'pass'
      : 'diagnostic-pass';
  return {
    status,
    selectedCases: selectedCases.map((example) => example.id),
    quadrants,
    crossComparisons,
    stabilityComparisons,
    coverage
  };
}
