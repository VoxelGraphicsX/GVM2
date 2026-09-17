#!/usr/bin/env node

import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

const scriptPath = fileURLToPath(import.meta.url);
const scriptDir = path.dirname(scriptPath);
const sourceDir = path.resolve(scriptDir, '..', '..', '..', '..');

function parseArgs(argv) {
  const [, , ...rest] = argv;
  const options = {};
  for (let index = 0; index < rest.length; index += 1) {
    const token = rest[index];
    if (!token.startsWith('--')) {
      continue;
    }

    const key = token.slice(2);
    const next = rest[index + 1];
    if (next && !next.startsWith('--')) {
      options[key] = next;
      index += 1;
    } else {
      options[key] = 'true';
    }
  }
  return options;
}

function parseBooleanOption(value, defaultValue = false) {
  if (value == null) {
    return defaultValue;
  }

  const normalized = String(value).trim().toLowerCase();
  if (['1', 'true', 'yes', 'on'].includes(normalized)) {
    return true;
  }
  if (['0', 'false', 'no', 'off'].includes(normalized)) {
    return false;
  }
  return defaultValue;
}

async function pathExists(targetPath) {
  try {
    await fs.access(targetPath);
    return true;
  } catch {
    return false;
  }
}

async function loadText(filePath) {
  return fs.readFile(filePath, 'utf8');
}

function formatRelative(targetPath) {
  const relative = path.relative(sourceDir, targetPath);
  if (!relative || relative === '') {
    return '.';
  }
  return relative.startsWith('..') ? targetPath : relative;
}

function collectQuotedPaths(cmakeScriptText) {
  const matches = [];
  const quotedPathPattern = /"([^"]+)"/g;
  let match = quotedPathPattern.exec(cmakeScriptText);
  while (match != null) {
    matches.push(match[1]);
    match = quotedPathPattern.exec(cmakeScriptText);
  }
  return matches;
}

async function collectExtraTargetArtifacts(buildDir, target) {
  const candidates = [
    path.join(buildDir, `${target}.dSYM`),
    path.join(buildDir, `${target}.exe`),
    path.join(buildDir, `${target}.ilk`),
    path.join(buildDir, `${target}.lib`),
    path.join(buildDir, `${target}.exp`),
    path.join(buildDir, `${target}.manifest`),
    path.join(buildDir, `${target}.pdb`)
  ];

  const existing = [];
  for (const candidate of candidates) {
    if (await pathExists(candidate)) {
      existing.push(candidate);
    }
  }
  return existing;
}

async function main() {
  const options = parseArgs(process.argv);
  const target = options.target ?? 'UGLC';
  const buildDir = path.resolve(sourceDir, options['build-dir'] ?? path.join('build', 'macos-tests-debug-make'));
  const dryRun = parseBooleanOption(options['dry-run'], false);

  const cmakeCachePath = path.join(buildDir, 'CMakeCache.txt');
  const targetDir = path.join(buildDir, 'CMakeFiles', `${target}.dir`);
  const cleanScriptPath = path.join(targetDir, 'cmake_clean.cmake');

  if (!(await pathExists(buildDir))) {
    throw new Error(`Build directory does not exist: ${buildDir}`);
  }
  if (!(await pathExists(cmakeCachePath))) {
    throw new Error(`This does not look like a configured CMake build directory: ${buildDir}`);
  }
  if (!(await pathExists(cleanScriptPath))) {
    throw new Error(`Could not find target clean script: ${cleanScriptPath}`);
  }

  const cleanScriptText = await loadText(cleanScriptPath);
  const cmakeManagedRelativePaths = collectQuotedPaths(cleanScriptText);
  const cmakeManagedPaths = cmakeManagedRelativePaths.map((entry) => path.resolve(buildDir, entry));
  const extraArtifacts = await collectExtraTargetArtifacts(buildDir, target);

  const removablePaths = [];
  for (const candidate of [...cmakeManagedPaths, ...extraArtifacts]) {
    if (await pathExists(candidate)) {
      removablePaths.push(candidate);
    }
  }

  const preservedPaths = [
    path.join(buildDir, '_deps', 'dxcompiler-ext-build'),
    path.join(buildDir, '_deps', 'dxcompiler-ext-install'),
    path.join(buildDir, '_deps', 'dxcompiler-src'),
    path.join(buildDir, '_deps', 'llvm'),
    path.join(buildDir, 'UGLCDxcExternal-prefix')
  ];

  console.log(`Target clean scope: ${target}`);
  console.log(`Build directory: ${formatRelative(buildDir)}`);
  console.log(`CMake clean script: ${formatRelative(cleanScriptPath)}`);
  console.log('');

  if (removablePaths.length === 0) {
    console.log('No target-local artifacts are currently present. Nothing to delete.');
  } else {
    console.log(`Planned removals (${removablePaths.length}):`);
    for (const targetPath of removablePaths) {
      console.log(`  - ${formatRelative(targetPath)}`);
    }
  }

  console.log('');
  console.log('Preserved caches:');
  for (const preservedPath of preservedPaths) {
    const exists = await pathExists(preservedPath);
    console.log(`  - ${formatRelative(preservedPath)}${exists ? '' : ' (not present in this build)'}`);
  }

  if (dryRun) {
    console.log('');
    console.log('Dry run only. No files were deleted.');
    return;
  }

  for (const targetPath of removablePaths) {
    await fs.rm(targetPath, { recursive: true, force: true });
  }

  console.log('');
  console.log(`Removed ${removablePaths.length} target artifact(s) for ${target}.`);
  console.log(`DXC/LLVM caches were preserved. You can now rebuild with: cmake --build ${formatRelative(buildDir)} --target ${target} -j4`);
}

main().catch((error) => {
  console.error(error instanceof Error ? `${error.message}\n${error.stack ?? ''}` : String(error));
  process.exitCode = 1;
});
