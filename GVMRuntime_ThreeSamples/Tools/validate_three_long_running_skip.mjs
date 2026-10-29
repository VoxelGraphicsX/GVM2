#!/usr/bin/env node

import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

const repositoryRoot = path.resolve(new URL('../..', import.meta.url).pathname);
export const LONG_RUNNING_SKIP_THRESHOLD_SECONDS = 3 * 60 * 60;

/** Reads one JSON document and reports a useful path-specific error. */
async function readJson(documentPath) {
  try {
    return JSON.parse(await fs.readFile(documentPath, 'utf8'));
  } catch (error) {
    throw new Error(`Could not read ${documentPath}: ${error.message}`);
  }
}

/** Resolves one repository-relative path without consulting environment variables. */
function resolveRepositoryPath(value) {
  return path.isAbsolute(value) ? value : path.resolve(repositoryRoot, value);
}

/** Builds a case-id to implementation-level map from all explicit shard inventories. */
async function loadImplementationLevels(dslRoot) {
  const levels = new Map();
  let entries;
  try {
    entries = await fs.readdir(dslRoot, { withFileTypes: true });
  } catch (error) {
    throw new Error(`Could not enumerate DSL root ${dslRoot}: ${error.message}`);
  }
  for (const entry of entries) {
    if (!entry.isDirectory()) continue;
    const inventoryPath = path.join(dslRoot, entry.name, 'implemented-cases.json');
    let inventory;
    try {
      inventory = JSON.parse(await fs.readFile(inventoryPath, 'utf8'));
    } catch (error) {
      if (error?.code === 'ENOENT') continue;
      throw new Error(`Could not read ${inventoryPath}: ${error.message}`);
    }
    for (const item of inventory.cases ?? []) {
      if (typeof item?.caseId !== 'string') continue;
      const previous = levels.get(item.caseId);
      if (previous && previous !== item.implementationLevel) {
        throw new Error(`Case '${item.caseId}' has conflicting implementation levels.`);
      }
      levels.set(item.caseId, item.implementationLevel);
    }
  }
  return levels;
}

/**
 * Validates the explicit evidence ledger used to defer a case after prolonged
 * failed convergence.  A ledger entry never changes the Manifest status and
 * never contributes to strict-pass or coverage counts.
 */
export async function validateLongRunningSkipLedger({
  manifest,
  ledger,
  dslRoot,
  requireEvidenceFiles = true
}) {
  const failures = [];
  const threshold = Number.isFinite(ledger?.thresholdSeconds)
    ? ledger.thresholdSeconds
    : LONG_RUNNING_SKIP_THRESHOLD_SECONDS;
  if (ledger?.schemaVersion !== 1) failures.push('schemaVersion must be 1.');
  if (!Number.isFinite(threshold) || threshold < LONG_RUNNING_SKIP_THRESHOLD_SECONDS) {
    failures.push(`thresholdSeconds must be at least ${LONG_RUNNING_SKIP_THRESHOLD_SECONDS}.`);
  }
  if (!Array.isArray(ledger?.entries)) {
    failures.push('entries must be an array.');
    return { status: 'fail', thresholdSeconds: threshold, entries: [], failures };
  }
  const manifestById = new Map((manifest?.examples ?? []).map((entry) => [entry.id, entry]));
  const implementationLevels = dslRoot ? await loadImplementationLevels(dslRoot) : new Map();
  const seen = new Set();
  const validStatuses = new Set(['eligible', 'selected']);
  for (const entry of ledger.entries) {
    const caseId = entry?.caseId;
    if (typeof caseId !== 'string' || caseId.length === 0) {
      failures.push('Every skip entry requires a caseId.');
      continue;
    }
    if (seen.has(caseId)) failures.push(`${caseId}: duplicate skip entry.`);
    seen.add(caseId);
    const example = manifestById.get(caseId);
    if (!example) {
      failures.push(`${caseId}: not present in the locked Manifest.`);
      continue;
    }
    if (example.status !== 'phase1_required') {
      failures.push(`${caseId}: only phase1_required cases may be long-running skipped.`);
    }
    if (!validStatuses.has(entry.status)) {
      failures.push(`${caseId}: status must be eligible or selected.`);
    }
    if (implementationLevels.get(caseId) === 'strict-pass') {
      failures.push(`${caseId}: strict-pass cases cannot be long-running skipped.`);
    }
    if (entry.reasonCode !== 'long_running_no_convergence') {
      failures.push(`${caseId}: reasonCode must be long_running_no_convergence.`);
    }
    if (!Array.isArray(entry.attempts) || entry.attempts.length < 3) {
      failures.push(`${caseId}: at least three failed attempts are required.`);
      continue;
    }
    let duration = 0;
    const reportPaths = new Set();
    for (const [index, attempt] of entry.attempts.entries()) {
      if (attempt?.result !== 'fail') {
        failures.push(`${caseId}: attempt ${index + 1} must have result=fail.`);
      }
      const seconds = Number(attempt?.durationSeconds);
      if (!Number.isFinite(seconds) || seconds <= 0) {
        failures.push(`${caseId}: attempt ${index + 1} requires positive durationSeconds.`);
      } else {
        duration += seconds;
      }
      if (typeof attempt?.reportPath !== 'string' || attempt.reportPath.length === 0) {
        failures.push(`${caseId}: attempt ${index + 1} requires reportPath evidence.`);
      } else {
        const reportPath = resolveRepositoryPath(attempt.reportPath);
        reportPaths.add(reportPath);
        if (requireEvidenceFiles) {
          try {
            await fs.access(reportPath);
          } catch {
            failures.push(`${caseId}: missing attempt report ${attempt.reportPath}.`);
          }
        }
      }
      if (!Array.isArray(attempt?.failures) || attempt.failures.length === 0) {
        failures.push(`${caseId}: attempt ${index + 1} must record concrete failure text.`);
      }
    }
    if (Number.isFinite(entry.totalAttemptSeconds)
        && Math.abs(entry.totalAttemptSeconds - duration) > 0.5) {
      failures.push(`${caseId}: totalAttemptSeconds does not equal attempt durations.`);
    }
    if (duration < threshold) {
      failures.push(`${caseId}: ${duration}s is below the ${threshold}s long-running threshold.`);
    }
    if (reportPaths.size < 3) {
      failures.push(`${caseId}: attempts must reference at least three distinct reports.`);
    }
    if (entry.status === 'selected' && !entry.selectedAt) {
      failures.push(`${caseId}: selected entries require selectedAt.`);
    }
  }
  return {
    status: failures.length === 0 ? 'pass' : 'fail',
    thresholdSeconds: threshold,
    entries: ledger.entries,
    failures
  };
}

/** Loads and validates a long-running skip ledger from the command line. */
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
  if (!options.manifest || !options.ledger) {
    throw new Error('Usage: validate_three_long_running_skip.mjs --manifest <path> --ledger <path> [--dsl-root <path>]');
  }
  const manifest = await readJson(resolveRepositoryPath(options.manifest));
  const ledger = await readJson(resolveRepositoryPath(options.ledger));
  const result = await validateLongRunningSkipLedger({
    manifest,
    ledger,
    dslRoot: options['dsl-root']
      ? resolveRepositoryPath(options['dsl-root'])
      : path.join(repositoryRoot, 'GVMRuntime_ThreeSamples', 'Dsl')
  });
  process.stdout.write(`${JSON.stringify(result, null, 2)}\n`);
  if (result.status !== 'pass') process.exitCode = 1;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
