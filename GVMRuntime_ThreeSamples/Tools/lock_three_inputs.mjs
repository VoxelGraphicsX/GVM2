#!/usr/bin/env node

import path from 'node:path';
import process from 'node:process';

import {
  assertThreeInputLock,
  generateThreeInputLock,
  writeThreeInputLock
} from './asset_oracle_lock.mjs';
import { defaultManifestPath } from './manifest_common.mjs';

/** Parses one command plus strict value-bearing long-form options. */
function parseArguments(argv) {
  const [, , command = 'help', ...tokens] = argv;
  const options = {};
  for (let index = 0; index < tokens.length; index += 2) {
    const option = tokens[index];
    const value = tokens[index + 1];
    if (!option?.startsWith('--') || value == null || value.startsWith('--')) {
      throw new Error(`Expected --option value pair near '${option ?? '<end>'}'.`);
    }
    options[option.slice(2)] = value;
  }
  return { command, options };
}

/** Resolves one required explicit filesystem option without consulting environment variables. */
function requirePathOption(options, name) {
  const value = options[name];
  if (!value) {
    throw new Error(`Missing required --${name} path.`);
  }
  return path.resolve(value);
}

/** Builds the shared generation and verification options from explicit CLI paths. */
function createLockOptions(options) {
  return {
    manifestPath: path.resolve(options.manifest ?? defaultManifestPath),
    upstreamRoot: requirePathOption(options, 'upstream-root'),
    assetPackRoot: requirePathOption(options, 'asset-pack-root'),
    oracleRoot: requirePathOption(options, 'oracle-root'),
    externalAssetMapPath: options['external-asset-map']
      ? path.resolve(options['external-asset-map'])
      : ''
  };
}

/** Prints the supported offline lock generation and verification commands. */
function printHelp() {
  console.log(`Usage:
  node GVMRuntime_ThreeSamples/Tools/lock_three_inputs.mjs generate \\
    --upstream-root <three-r185-checkout> --asset-pack-root <asset-pack> \\
    --oracle-root <oracle-pack> --output <lock.json> [--manifest <manifest.json>] \\
    [--external-asset-map <map.json>]

  node GVMRuntime_ThreeSamples/Tools/lock_three_inputs.mjs verify \\
    --upstream-root <three-r185-checkout> --asset-pack-root <asset-pack> \\
    --oracle-root <oracle-pack> --lock <lock.json> [--manifest <manifest.json>] \\
    [--external-asset-map <map.json>]

--external-asset-map is required only when a phase1_required example has a static
external HTTP(S) dependency. The command performs local filesystem reads only
and never downloads dependencies.`);
}

/** Generates or verifies the immutable Three source, asset, and Oracle input lock. */
async function main() {
  const { command, options } = parseArguments(process.argv);
  if (command === 'help' || command === '--help' || command === '-h') {
    printHelp();
    return;
  }
  const lockOptions = createLockOptions(options);
  if (command === 'generate') {
    const outputPath = requirePathOption(options, 'output');
    const lock = await generateThreeInputLock(lockOptions);
    await writeThreeInputLock(outputPath, lock);
    console.log(`Three r185 input lock: ${outputPath}`);
    console.log(`Locked files=${lock.files.length} scanned_examples=${lock.scope.scannedExamples} required_scenarios=${lock.scope.requiredScenarios}`);
    return;
  }
  if (command === 'verify') {
    const result = await assertThreeInputLock({ ...lockOptions, lockPath: requirePathOption(options, 'lock') });
    console.log(`Three r185 input preflight passed: files=${result.fileCount} scanned_examples=${result.scannedExamples} required_scenarios=${result.requiredScenarios}`);
    return;
  }
  printHelp();
  throw new Error(`Unknown command '${command}'.`);
}

main().catch((error) => {
  console.error(error instanceof Error ? error.stack ?? error.message : String(error));
  process.exitCode = 1;
});
