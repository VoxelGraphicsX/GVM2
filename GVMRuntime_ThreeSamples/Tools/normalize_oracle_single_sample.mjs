#!/usr/bin/env node

import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { pathToFileURL } from 'node:url';

/** Recursively collects JSON sidecars below one locked Oracle root. */
async function collectJsonFiles(directoryPath) {
  const entries = await fs.readdir(directoryPath, { withFileTypes: true });
  const files = [];
  for (const entry of entries) {
    const entryPath = path.join(directoryPath, entry.name);
    if (entry.isDirectory()) files.push(...await collectJsonFiles(entryPath));
    else if (entry.isFile() && entry.name.endsWith('.json')) files.push(entryPath);
  }
  return files;
}

/** Adds the explicit ordinary single-sample policy to selected Three Oracle sidecars. */
export async function normalizeOracleRoot(directoryPath, caseIds = null) {
  const files = await collectJsonFiles(directoryPath);
  const selected = caseIds == null
    ? files
    : files.filter((filePath) => caseIds.some((caseId) => filePath.includes(`${path.sep}${caseId}${path.sep}`)));
  let changed = 0;
  for (const filePath of selected) {
    if (filePath.endsWith('.semantic.json')) continue;
    let document;
    try {
      document = JSON.parse(await fs.readFile(filePath, 'utf8'));
    } catch {
      continue;
    }
    if (!document || typeof document !== 'object' || Array.isArray(document)) continue;
    const policy = document.samplePolicy;
    if (policy?.mode === 'single-sample'
      && policy.msaaEnabled === false
      && policy.simulateMsaa === false) continue;
    document.samplePolicy = {
      mode: 'single-sample',
      msaaEnabled: false,
      simulateMsaa: false
    };
    await fs.writeFile(filePath, `${JSON.stringify(document, null, 2)}\n`, 'utf8');
    changed += 1;
  }
  return { scanned: selected.length, changed };
}

/** Runs the explicit Oracle sidecar maintenance command without environment fallbacks. */
async function main() {
  const rootIndex = process.argv.indexOf('--root');
  if (rootIndex < 0 || process.argv[rootIndex + 1] == null) {
    throw new Error('Usage: normalize_oracle_single_sample.mjs --root <oracle-root> [--case-ids id1,id2]');
  }
  const root = path.resolve(process.argv[rootIndex + 1]);
  const caseIndex = process.argv.indexOf('--case-ids');
  const caseIds = caseIndex >= 0
    ? process.argv[caseIndex + 1].split(',').filter(Boolean)
    : null;
  console.log(JSON.stringify(await normalizeOracleRoot(root, caseIds)));
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
