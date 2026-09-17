#!/usr/bin/env node

import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';

/**
 * Describes the deterministic structural contract for the current 20-case wave.
 * This table intentionally does not claim that the upstream algorithms are implemented.
 */
export const structuralCases = Object.freeze({
  webgl_geometry_shapes: [93, 93, 1],
  webgl_effects_ascii: [2, 2, 1],
  webgl_interactive_cubes: [2000, 2000, 1],
  webgl_interactive_cubes_ortho: [2000, 2000, 1],
  webgl_interactive_lines: [51, 51, 1],
  webgl_interactive_raycasting_points: [43, 43, 1],
  webgl_loader_3ds: [1, 1, 1],
  webgl_loader_obj: [14, 14, 1],
  webgl_lod: [5000, 5000, 1],
  webgl_materials_cubemap: [3, 3, 1],
  webgl_materials_cubemap_refraction: [3, 3, 1],
  webgl_materials_cubemap_mipmaps: [2, 2, 1],
  webgl_materials_cubemap_render_to_mipmaps: [2, 2, 1],
  webgl_materials_physical_clearcoat: [5, 5, 1],
  webgl_modifier_simplifier: [2, 2, 1],
  webgl_points_dynamic: [73, 73, 1],
  webgl_postprocessing_backgrounds: [1, 1, 1],
  webgl_postprocessing_glitch: [1, 1, 100],
  webgpu_depth_texture: [50, 50, 1],
  webgpu_fog_height: [1, 1, 100]
});

/** Parses explicit --option value pairs without environment fallbacks. */
export function parseArguments(argv) {
  const options = {};
  for (let index = 2; index < argv.length; index += 2) {
    const option = argv[index];
    const value = argv[index + 1];
    if (!option?.startsWith('--') || value == null || value.startsWith('--')) {
      throw new Error(`Expected --option value pair near '${option ?? '<end>'}'.`);
    }
    options[option.slice(2)] = value;
  }
  return options;
}

/** Validates one structural scene snapshot and returns all contract violations. */
export function validateSnapshot(snapshot) {
  const errors = [];
  const expected = structuralCases[snapshot?.caseId];
  if (!expected) return [`unknown structural case '${snapshot?.caseId ?? '<missing>'}'`];
  const [renderableCount, entityCount, instanceCount] = expected;
  const equal = (field, value) => {
    if (snapshot[field] !== value) errors.push(`${field} expected ${value}, got ${snapshot[field]}`);
  };
  equal('renderSetPolicy', 'required');
  equal('gpuWorkDslOnly', true);
  equal('sceneRenderSetCount', 1);
  equal('renderableObjectCount', renderableCount);
  equal('entityCount', entityCount);
  equal('instanceCount', instanceCount);
  equal('sampleCount', 1);
  equal('msaaEnabled', false);
  equal('directDrawFallback', false);
  equal('assetAndAlgorithmState', 'structural-fixture-only');
  if (typeof snapshot.renderSetType !== 'string' || snapshot.renderSetType.length === 0) {
    errors.push('renderSetType must be a non-empty string');
  }
  if (!Array.isArray(snapshot.instanceCounts)
    || snapshot.instanceCounts.length !== entityCount
    || snapshot.instanceCounts.some((count) => count !== instanceCount)) {
    errors.push(`instanceCounts must contain ${entityCount} entries equal to ${instanceCount}`);
  }
  if (!Number.isInteger(snapshot.scenePassCount) || snapshot.scenePassCount < 1) {
    errors.push('scenePassCount must be a positive integer');
  }
  if (!Number.isInteger(snapshot.screenPassCount) || snapshot.screenPassCount < 0) {
    errors.push('screenPassCount must be a non-negative integer');
  }
  return errors;
}

/** Recursively finds scene snapshots emitted by the structural fixture. */
async function collectSnapshots(root) {
  const entries = await fs.readdir(root, { withFileTypes: true });
  const snapshots = [];
  for (const entry of entries) {
    const entryPath = path.join(root, entry.name);
    if (entry.isDirectory()) snapshots.push(...await collectSnapshots(entryPath));
    else if (entry.isFile() && entry.name.endsWith('.scene.json')) snapshots.push(entryPath);
  }
  return snapshots;
}

/** Validates all available snapshots; --require-all enforces one for every active case. */
export async function validateDirectory(root, requireAll = false) {
  const files = await collectSnapshots(root);
  const seen = new Map();
  const records = [];
  const failures = [];
  for (const file of files) {
    let snapshot;
    try {
      snapshot = JSON.parse(await fs.readFile(file, 'utf8'));
    } catch (error) {
      failures.push(`${file}: invalid JSON (${error.message})`);
      continue;
    }
    const errors = validateSnapshot(snapshot);
    if (errors.length > 0) failures.push(`${file}: ${errors.join('; ')}`);
    seen.set(snapshot.caseId, (seen.get(snapshot.caseId) ?? 0) + 1);
    const metadataPath = file.replace(/\.scene\.json$/u, '.json');
    let metadata = null;
    try {
      metadata = JSON.parse(await fs.readFile(metadataPath, 'utf8'));
    } catch {
      // Metadata is optional for direct unit-test snapshots.
    }
    records.push({
      caseId: snapshot.caseId,
      snapshotPath: file,
      pipeline: metadata?.pipeline ?? (/-experimental\.scene\.json$/u.test(file) ? 'experimental' : 'legacy'),
      backend: metadata?.backend ?? 'unknown',
      scenarioId: snapshot.scenarioId ?? null,
      errors
    });
  }
  if (requireAll) {
    for (const caseId of Object.keys(structuralCases)) {
      if (!seen.has(caseId)) failures.push(`missing structural snapshot for ${caseId}`);
    }
  }
  return { files, seen, records, failures };
}

/** Builds a machine-readable, non-strict evidence summary for the structural fixture. */
export function buildSummary(result, root) {
  const caseResults = Object.keys(structuralCases).map((caseId) => {
    const records = result.records.filter((record) => record.caseId === caseId);
    return {
      caseId,
      status: records.length > 0 && records.every((record) => record.errors.length === 0) ? 'pass' : 'missing-or-failed',
      snapshotCount: records.length,
      renderableObjectCount: structuralCases[caseId][0],
      entityCount: structuralCases[caseId][1],
      instanceCount: structuralCases[caseId][2],
      pipelines: [...new Set(records.map((record) => record.pipeline))].sort(),
      backends: [...new Set(records.map((record) => record.backend))].sort(),
      snapshotPaths: records.map((record) => path.relative(root, record.snapshotPath)).sort()
    };
  });
  return {
    evidenceType: 'structural-fixture',
    status: result.failures.length === 0 ? 'pass' : 'fail',
    strict: false,
    caseCount: caseResults.filter((entry) => entry.snapshotCount > 0).length,
    snapshotCount: result.records.length,
    root,
    failures: result.failures,
    caseResults
  };
}

if (import.meta.url === `file://${process.argv[1]}`) {
  const options = parseArguments(process.argv);
  if (!options['snapshot-root']) throw new Error('Missing --snapshot-root path.');
  const result = await validateDirectory(path.resolve(options['snapshot-root']), options['require-all'] === 'true');
  const root = path.resolve(options['snapshot-root']);
  const summary = buildSummary(result, root);
  console.log(JSON.stringify({
    snapshotCount: result.files.length,
    caseCount: result.seen.size,
    failures: result.failures
  }, null, 2));
  if (options.summary) {
    const summaryPath = path.resolve(options.summary);
    await fs.mkdir(path.dirname(summaryPath), { recursive: true });
    await fs.writeFile(summaryPath, `${JSON.stringify(summary, null, 2)}\n`, 'utf8');
  }
  if (result.failures.length > 0) process.exitCode = 1;
}
