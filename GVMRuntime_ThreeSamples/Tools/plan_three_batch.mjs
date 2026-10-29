#!/usr/bin/env node

import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

const repositoryRoot = path.resolve(new URL('../..', import.meta.url).pathname);

/** Loads one explicit JSON path. */
async function readJson(value) {
  const filePath = path.isAbsolute(value) ? value : path.resolve(repositoryRoot, value);
  return JSON.parse(await fs.readFile(filePath, 'utf8'));
}

/** Reads implementation inventories and returns the strongest declaration per case. */
async function loadInventories(dslRoot) {
  const result = new Map();
  const entries = await fs.readdir(dslRoot, { withFileTypes: true });
  for (const entry of entries) {
    if (!entry.isDirectory()) continue;
    const inventoryPath = path.join(dslRoot, entry.name, 'implemented-cases.json');
    let inventory;
    try {
      inventory = JSON.parse(await fs.readFile(inventoryPath, 'utf8'));
    } catch (error) {
      if (error?.code === 'ENOENT') continue;
      throw error;
    }
    for (const item of inventory.cases ?? []) {
      if (typeof item?.caseId !== 'string') continue;
      const current = result.get(item.caseId);
      const rank = { scaffolded: 1, 'semantic-complete': 2, 'strict-pass': 3 };
      if (!current || (rank[item.implementationLevel] ?? 0) > (rank[current.implementationLevel] ?? 0)) {
        result.set(item.caseId, { ...item, shard: entry.name });
      }
    }
  }
  return result;
}

/** Selects the next deterministic work batch without treating planning as implementation. */
export function selectNextThreeBatch({
  manifest,
  inventories,
  strictCaseIds = [],
  skippedCaseIds = [],
  minimumCaseCount = 20
}) {
  if (!Array.isArray(manifest?.examples) || manifest.examples.length !== 588) {
    throw new Error('The source Three r185 Manifest must contain exactly 588 examples.');
  }
  if (!Number.isInteger(minimumCaseCount) || minimumCaseCount < 20) {
    throw new Error('minimumCaseCount must be at least 20.');
  }
  // A strict inventory declaration is authoritative even when an older
  // baseline ledger has not yet been refreshed.  Planning must never put an
  // already strict case back into a net-new batch.
  const strict = new Set(strictCaseIds);
  for (const [caseId, implementation] of inventories.entries()) {
    if (implementation?.implementationLevel === 'strict-pass') {
      strict.add(caseId);
    }
  }
  const skipped = new Set(skippedCaseIds);
  const rank = { 'semantic-complete': 0, scaffolded: 1, unimplemented: 2 };
  const candidates = manifest.examples
    .filter((example) => example.status === 'phase1_required')
    .filter((example) => !strict.has(example.id) && !skipped.has(example.id))
    .map((example, order) => {
      const implementation = inventories.get(example.id);
      const level = implementation?.implementationLevel === 'strict-pass'
        ? 'strict-pass'
        : implementation?.implementationLevel ?? 'unimplemented';
      return { example, implementation, level, order };
    })
    .sort((left, right) => (rank[left.level] ?? 3) - (rank[right.level] ?? 3)
      || (left.order - right.order));
  if (candidates.length < minimumCaseCount) {
    throw new Error(`Only ${candidates.length} eligible cases remain; cannot form a ${minimumCaseCount}-case batch.`);
  }
  const activeCases = candidates.slice(0, minimumCaseCount).map(({ example, implementation, level }) => ({
    caseId: example.id,
    dslEntry: implementation?.dslEntry ?? `${example.dslShard}Renderer`,
    hostTarget: implementation?.hostTarget ?? example.dslShard,
    dslShard: implementation?.shard ?? example.dslShard,
    implementationLevel: level,
    readyForRunner: level !== 'unimplemented'
  }));
  return {
    schemaVersion: 1,
    batchId: `three-r185-next-batch-${Date.now()}`,
    manifestPath: 'GVMRuntime_ThreeSamples/Manifest/three-r185-manifest.json',
    minimumCaseCount,
    selectionPolicy: {
      singleSample: true,
      msaaEnabled: false,
      simulateMsaa: false,
      excludeVideoCameraWebxrTiming: true,
      deferredCapabilitySetIsFrozen: true
    },
    upstreamMsaaPolicy: {
      enabled: false,
      simulateMsaa: false,
      note: '上游即使声明 MSAA，本阶段也采用普通单采样绘制，不做软件或着色器模拟。'
    },
    activeCases,
    notYetRunnableCount: activeCases.filter((entry) => !entry.readyForRunner).length,
    notes: '这是实现队列，不是通过声明。只有每项完成专属 DSL/Host、资产与 Oracle、四象限三次稳定和 lint 后，才可交给 run_three_batch.mjs。'
  };
}

/** Writes the next deterministic twenty-case implementation queue. */
async function main() {
  const args = process.argv.slice(2);
  const options = {};
  for (let index = 0; index < args.length; index += 2) {
    const option = args[index];
    const value = args[index + 1];
    if (!option?.startsWith('--') || value == null || value.startsWith('--')) {
      throw new Error(`Expected --option value near '${option ?? '<end>'}'.`);
    }
    options[option.slice(2)] = value;
  }
  if (!options.manifest || !options.output) {
    throw new Error('Usage: plan_three_batch.mjs --manifest <path> --dsl-root <path> --output <path> [--strict-ledger <path>] [--skip-ledger <path>]');
  }
  const manifest = await readJson(options.manifest);
  const inventories = await loadInventories(path.isAbsolute(options['dsl-root'])
    ? options['dsl-root'] : path.resolve(repositoryRoot, options['dsl-root'] ?? 'GVMRuntime_ThreeSamples/Dsl'));
  const strictLedger = options['strict-ledger'] ? await readJson(options['strict-ledger']) : {};
  const skipLedger = options['skip-ledger'] ? await readJson(options['skip-ledger']) : {};
  const batch = selectNextThreeBatch({
    manifest,
    inventories,
    strictCaseIds: strictLedger.strictCaseIds ?? [],
    skippedCaseIds: (skipLedger.entries ?? []).map((entry) => entry.caseId)
  });
  const outputPath = path.isAbsolute(options.output) ? options.output : path.resolve(repositoryRoot, options.output);
  await fs.writeFile(outputPath, `${JSON.stringify(batch, null, 2)}\n`, 'utf8');
  process.stdout.write(`${JSON.stringify({
    output: path.relative(repositoryRoot, outputPath),
    caseCount: batch.activeCases.length,
    notYetRunnableCount: batch.notYetRunnableCount,
    caseIds: batch.activeCases.map((entry) => entry.caseId)
  }, null, 2)}\n`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
