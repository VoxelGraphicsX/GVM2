#!/usr/bin/env node

import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { pathToFileURL } from 'node:url';

const forbiddenSourcePattern =
  /supersampl|\bMSAA\b|antialias\s*resolve|coverage samples?|four[-\s]sample|rotated[-\s]grid[-\s](?:sample|resolve)|sampleColor[0-9]|sceneTexture[1-3]|sampleTexture[1-3]|sampleUniformBuffer[1-3]|sampleBuffer[1-3]/iu;
const sourceExtensionPattern = /\.(?:cpp|h|hpp|mjs)$/u;

/** Recursively returns repository source files below one directory. */
async function collectSourceFiles(directoryPath) {
  const entries = await fs.readdir(directoryPath, { withFileTypes: true });
  const files = [];
  for (const entry of entries) {
    const entryPath = path.join(directoryPath, entry.name);
    if (entry.isDirectory()) files.push(...await collectSourceFiles(entryPath));
    else if (entry.isFile() && sourceExtensionPattern.test(entry.name)) files.push(entryPath);
  }
  return files;
}

/** Removes C/C++ comments so explanatory single-sample documentation is not treated as a feature. */
function stripComments(source) {
  let inBlockComment = false;
  return source.split(/\r?\n/u).map((line) => {
    let output = '';
    let index = 0;
    while (index < line.length) {
      if (inBlockComment) {
        const end = line.indexOf('*/', index);
        if (end < 0) return output;
        inBlockComment = false;
        index = end + 2;
        continue;
      }
      if (line.startsWith('//', index)) break;
      if (line.startsWith('/*', index)) {
        inBlockComment = true;
        index += 2;
        continue;
      }
      output += line[index];
      index += 1;
    }
    return output;
  });
}

/** Reports DSL source lines that enable or describe forbidden multisample simulation. */
export async function lintSingleSampleSources(dslRoot) {
  const failures = [];
  for (const sourcePath of await collectSourceFiles(dslRoot)) {
    const source = await fs.readFile(sourcePath, 'utf8');
    const lines = stripComments(source);
    for (let index = 0; index < lines.length; index += 1) {
      if (forbiddenSourcePattern.test(lines[index])) {
        failures.push(`${sourcePath}:${index + 1}: ${lines[index].trim()}`);
      }
    }
  }
  return failures;
}

/** Verifies the frozen manifest declares the global direct single-sample policy. */
export async function lintManifestSingleSamplePolicy(manifestPath) {
  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  const policy = manifest.samplePolicy;
  if (policy?.mode !== 'single-sample'
    || policy.msaaEnabled !== false
    || policy.simulateMsaa !== false
    || policy.upstreamMsaaDoesNotExcludeExample !== true) {
    return [`${manifestPath}: samplePolicy does not enforce direct single-sample rendering.`];
  }
  return [];
}

/** Runs the repository single-sample policy lint with explicit paths. */
async function main() {
  const dslIndex = process.argv.indexOf('--dsl-root');
  const manifestIndex = process.argv.indexOf('--manifest');
  if (dslIndex < 0 || manifestIndex < 0
    || process.argv[dslIndex + 1] == null || process.argv[manifestIndex + 1] == null) {
    throw new Error('Usage: lint_single_sample_policy.mjs --dsl-root <path> --manifest <path>');
  }
  const dslRoot = path.resolve(process.argv[dslIndex + 1]);
  const manifestPath = path.resolve(process.argv[manifestIndex + 1]);
  const failures = [
    ...await lintSingleSampleSources(dslRoot),
    ...await lintManifestSingleSamplePolicy(manifestPath)
  ];
  if (failures.length > 0) {
    console.error(failures.join('\n'));
    process.exitCode = 1;
    return;
  }
  console.log(`Single-sample policy lint passed: ${dslRoot}`);
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
