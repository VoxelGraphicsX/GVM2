#!/usr/bin/env node

import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath, pathToFileURL } from 'node:url';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../..');

/** Parses explicit value-bearing command-line options for report normalization. */
export function parseNormalizationArguments(argv) {
  const options = {};
  for (let index = 2; index < argv.length; index += 2) {
    const option = argv[index];
    const value = argv[index + 1];
    if (!option?.startsWith('--') || value == null || value.startsWith('--')) {
      throw new Error(`Expected --option value pair near '${option ?? '<end>'}'.`);
    }
    const name = option.slice(2);
    if (Object.hasOwn(options, name)) throw new Error(`Duplicate --${name}.`);
    options[name] = value;
  }
  for (const name of ['input', 'output', 'case-id']) {
    if (!options[name]) throw new Error(`Missing --${name}.`);
  }
  return options;
}

/** Returns a pass/fail status without changing the source report's metrics. */
function entryStatus(entry) {
  if (entry?.status === 'pass' || entry?.pass === true) return 'pass';
  if (Array.isArray(entry?.failures) && entry.failures.length === 0) return 'pass';
  return entry?.status ?? 'fail';
}

/** Copies one execution record into the common strict-run representation. */
function normalizeExecution(entry, caseId, scenarioId = null) {
  return {
    caseId,
    scenarioId: entry.scenarioId ?? entry.scenario ?? scenarioId,
    pipeline: entry.pipeline,
    backend: entry.backend,
    repetition: entry.repetition ?? entry.rep ?? entry.runIndex,
    status: entryStatus(entry),
    artifacts: {
      rgbaPath: entry.artifacts?.rgbaPath ?? entry.rgbaPath,
      metadataPath: entry.artifacts?.metadataPath ?? entry.metadataPath
    }
  };
}

/** Converts row/repeat evidence only when every run retains its artifact paths. */
function normalizeRows(report, caseId) {
  const runs = [];
  const failures = [];
  for (const row of report.rows ?? []) {
    for (const repeat of row.repeats ?? []) {
      const execution = normalizeExecution({
        ...repeat,
        pipeline: row.pipeline,
        backend: row.backend,
        scenarioId: row.scenario ?? row.scenarioId
      }, caseId);
      if (!execution.artifacts.rgbaPath || !execution.artifacts.metadataPath) {
        failures.push(`rows schema 缺少 ${row.scenario ?? row.scenarioId}/${row.pipeline}/${row.backend}/rep-${repeat.rep ?? repeat.repetition} 的 RGBA 或 metadata 路径。`);
      }
      runs.push(execution);
    }
  }
  return {runs, failures};
}

/** Normalizes one legacy strict report while retaining an immutable provenance block. */
export function normalizeStrictCaseReport(report, caseId, sourcePath = '<input>') {
  if (!report || typeof report !== 'object') throw new Error('Strict report must be an object.');
  const provenance = {
    sourcePath,
    sourceSchemaVersion: report.schemaVersion ?? null,
    sourceEvidenceType: report.evidenceType ?? null,
    conversion: '字段标准化；图像指标、路径、状态和采样策略原样保留；不补造证据。'
  };

  if (Array.isArray(report.runs) && Array.isArray(report.comparisons)) {
    return {
      ...report,
      schemaVersion: Math.max(2, Number(report.schemaVersion) || 0),
      caseId: report.caseId ?? caseId,
      normalization: { status: 'pass', provenance }
    };
  }

  if (Array.isArray(report.rows)) {
    const {runs, failures} = normalizeRows(report, caseId);
    if (failures.length > 0) {
      return {
        ...report,
        schemaVersion: 2,
        caseId: report.caseId ?? caseId,
        runs,
        comparisons: report.comparisons ?? [],
        normalization: { status: 'blocked', provenance, failures }
      };
    }
    return {
      ...report,
      schemaVersion: 2,
      caseId: report.caseId ?? caseId,
      executionCount: runs.length,
      oracleComparisonCount: report.oracleComparisonCount ?? runs.length,
      crossQuadrantComparisonCount: report.crossQuadrantComparisonCount ?? 0,
      stabilityComparisonCount: report.stabilityComparisonCount ?? 0,
      runs,
      comparisons: report.comparisons ?? [],
      normalization: { status: 'pass', provenance }
    };
  }

  if (Array.isArray(report.quadrants)) {
    const runs = report.quadrants
      .filter((entry) => entry.caseId == null || entry.caseId === caseId)
      .map((entry) => normalizeExecution(entry, caseId));
    const missingArtifacts = runs.filter((entry) => (
      !entry.artifacts.rgbaPath || !entry.artifacts.metadataPath
    )).length;
    if (missingArtifacts > 0) {
      return {
        ...report,
        schemaVersion: 2,
        caseId: report.caseId ?? caseId,
        runs,
        comparisons: [
          ...(report.crossComparisons ?? []),
          ...(report.stabilityComparisons ?? [])
        ],
        normalization: {
          status: 'blocked',
          provenance,
          failures: [`${missingArtifacts} 条四象限记录缺少 RGBA 或 metadata 路径。`]
        }
      };
    }
    return {
      ...report,
      schemaVersion: 2,
      caseId: report.caseId ?? caseId,
      executionCount: report.executionCount ?? runs.length,
      oracleComparisonCount: report.oracleComparisonCount ?? runs.length,
      crossQuadrantComparisonCount: report.crossQuadrantComparisonCount
        ?? (report.crossComparisons?.length ?? 0),
      stabilityComparisonCount: report.stabilityComparisonCount
        ?? (report.stabilityComparisons?.length ?? 0),
      runs,
      comparisons: [
        ...(report.crossComparisons ?? []),
        ...(report.stabilityComparisons ?? [])
      ],
      normalization: { status: 'pass', provenance }
    };
  }

  return {
    ...report,
    schemaVersion: 2,
    caseId: report.caseId ?? caseId,
    normalization: {
      status: 'blocked',
      provenance,
      failures: ['报告没有可标准化的 runs、rows 或 quadrants 证据。']
    }
  };
}

/** Normalizes one report file and writes the result without changing source evidence. */
export async function executeNormalization(argv = process.argv) {
  const options = parseNormalizationArguments(argv);
  const inputPath = path.resolve(repositoryRoot, options.input);
  const outputPath = path.resolve(repositoryRoot, options.output);
  const report = JSON.parse(await fs.readFile(inputPath, 'utf8'));
  const normalized = normalizeStrictCaseReport(report, options['case-id'], options.input);
  await fs.mkdir(path.dirname(outputPath), {recursive: true});
  await fs.writeFile(outputPath, `${JSON.stringify(normalized, null, 2)}\n`, 'utf8');
  process.stdout.write(`${JSON.stringify({output: options.output, status: normalized.normalization.status}, null, 2)}\n`);
  return normalized;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  executeNormalization().catch((error) => {
    process.stderr.write(`${error.stack ?? error.message}\n`);
    process.exitCode = 1;
  });
}
