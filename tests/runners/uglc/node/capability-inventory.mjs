#!/usr/bin/env node
import { promises as fs } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const sourceDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');

/** Collects actual intrinsic tags from generated IR; source text matches alone do not count as execution. */
function collectIntrinsicKinds(node, result) {
  if (Array.isArray(node)) { for (const entry of node) collectIntrinsicKinds(entry, result); }
  else if (node && typeof node === 'object') {
    if (node.intrinsicKind && node.intrinsicKind !== 'none') result.add(node.intrinsicKind);
    for (const value of Object.values(node)) collectIntrinsicKinds(value, result);
  }
}

/** Builds a reviewable inventory from a concrete compiler run without claiming an exhaustive formal DSL contract. */
async function main() {
  const args = process.argv.slice(2);
  const options = {};
  for (let index = 0; index < args.length; index += 2) options[args[index]?.replace(/^--/u, '')] = args[index + 1];
  if (!options.report || !options.output) throw new Error('Specify --report <UGLC summary.json> and --output <inventory.json>.');
  const reportPath = path.resolve(options.report);
  const report = JSON.parse(await fs.readFile(reportPath, 'utf8'));
  if (!(report.totals?.total > 0)) throw new Error('Inventory requires actual compiler test results.');
  const dumpSource = await fs.readFile(path.join(sourceDir, 'UGLC/Source/CodeGen/UGLIR/UGLIRDump.cpp'), 'utf8');
  const definitions = [...dumpSource.matchAll(/case IntrinsicCallKind::(\w+):\s*return "([^"]+)";/gu)].filter(match => match[1] !== 'None');
  const testsByKind = new Map();
  let moduleCount = 0;
  for (const test of report.cases ?? []) {
    if (test.status !== 'pass') continue;
    const directory = path.join(path.dirname(reportPath), 'cases', test.id, 'generated', 'uglir');
    let files;
    try { files = await fs.readdir(directory); } catch (error) { if (error.code === 'ENOENT') continue; throw error; }
    const kinds = new Set();
    for (const file of files.filter(file => file.endsWith('.json'))) {
      collectIntrinsicKinds(JSON.parse(await fs.readFile(path.join(directory, file), 'utf8')), kinds);
      moduleCount += 1;
    }
    for (const kind of kinds) {
      if (!testsByKind.has(kind)) testsByKind.set(kind, []);
      testsByKind.get(kind).push(test.id);
    }
  }
  if (!moduleCount || !definitions.length) throw new Error('No UGLIR module or intrinsic definitions were inspected.');
  const intrinsics = definitions.map(([, symbol, kind]) => ({ id: `intrinsic.${kind}`, symbol,
    testIds: (testsByKind.get(kind) ?? []).sort(), evidenceLevel: testsByKind.has(kind) ? 'compiled-ir-observed' : 'uncovered',
    gpuOracleVerified: false, requiredForFullReplacement: true }));
  const inventory = {
    schemaVersion: 1, legacyEnabled: true, defaultPipeline: 'UGLIR', formalContractComplete: false,
    sourceRun: report.run.runId, sourceSha256: report.evidence?.sourceSha256 ?? null, inspectedModules: moduleCount,
    note: 'This records concrete IR coverage. It does not redefine the formal DSL contract or infer GPU correctness from emitted intrinsics.',
    scopeDecision: 'Hull and Domain were explicitly excluded by the user on 2026-09-06; UGLIR is the default pipeline.',
    stages: [
      { id: 'compute', testIds: ['experimental-uglir-compute-basic-msl-direct-spirv', 'experimental-uglir-evaluation-semantics'], status: 'implemented' },
      { id: 'vertex', testIds: ['experimental-uglir-stage-io-semantics', 'experimental-uglir-render-set-shader-abi'], status: 'implemented' },
      { id: 'fragment', testIds: ['experimental-uglir-render-basic-msl-direct-spirv', 'experimental-uglir-rgba32float-framebuffer'], status: 'implemented' },
      { id: 'pixel-local', testIds: ['experimental-uglir-pixel-local-deferred-screen'], status: 'implemented' },
      { id: 'hull', testIds: ['experimental-uglir-excluded-hull-domain'], status: 'excluded-by-user', requiredForFormalSupport: false },
      { id: 'domain', testIds: ['experimental-uglir-excluded-hull-domain'], status: 'excluded-by-user', requiredForFormalSupport: false }
    ],
    resources: [
      { id: 'uniform-storage-buffers', testIds: ['experimental-uglir-buffer-layout-semantics', 'experimental-uglir-uniform-buffer-compute'] },
      { id: 'sampled-storage-textures', testIds: ['experimental-uglir-storage-texture-compute', 'experimental-uglir-texture2darray-gather', 'experimental-uglir-symbolic-resources'] },
      { id: 'binding-render-set', testIds: ['experimental-uglir-render-set-shader-abi', 'experimental-uglir-stage-io-semantics'] }
    ],
    blockingGaps: [
      'Audit every formally promised DSL feature, format and legal layout; the inventory is not yet exhaustive.',
      'Add GPU oracles for uncovered intrinsics, integer widths, all accepted layouts and precision behavior.',
      'Complete native Vulkan device evidence, frozen ThreeSamples inputs, sustained correctness and three identical-candidate matrices.'
    ],
    intrinsics
  };
  await fs.mkdir(path.dirname(path.resolve(options.output)), { recursive: true });
  await fs.writeFile(path.resolve(options.output), JSON.stringify(inventory, null, 2) + '\n');
  console.log(`Inspected ${moduleCount} modules; ${intrinsics.filter(entry => entry.testIds.length).length}/${intrinsics.length} IR intrinsic kinds have compile coverage. Formal contract completeness remains false.`);
}

await main();
