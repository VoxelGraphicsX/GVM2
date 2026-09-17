#!/usr/bin/env node
import { promises as fs } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { inspectGoogleTestReport, inspectReadbackReport } from '../../node/execution-contract.mjs';
import { comparePairedMeasurements } from './readiness-metrics.mjs';

/** Audits one complete Node report without treating a successful process or a partial selection as acceptance. */
export function auditNodeReport(report, specification, failures) {
  const prefix = specification.id;
  if (report.run?.group !== 'all' || report.run?.exitCode !== 0 || report.evidenceError) failures.push(`${prefix}: incomplete or failed full-suite run.`);
  if (report.evidence?.sourceSha256 !== specification.sourceSha256) failures.push(`${prefix}: candidate source digest mismatch.`);
  if (!report.evidence?.compiler?.sha256 || !report.evidence?.gpuInventory?.available) failures.push(`${prefix}: missing compiler or device provenance.`);
  if (report.run?.legacyEnabled !== specification.legacyEnabled) failures.push(`${prefix}: Legacy build state mismatch.`);
  if (!report.totals || report.totals.total <= 0 || report.totals.fail !== 0) failures.push(`${prefix}: missing execution or failed cases.`);
  if (!Array.isArray(specification.requiredIds) || specification.requiredIds.length === 0) {
    failures.push(`${prefix}: the independent required-case inventory is empty.`);
    return;
  }
  if (specification.kind === 'uglc') {
    const actual = report.cases ?? [];
    const ids = actual.map(entry => entry.id);
    if (new Set(ids).size !== ids.length || actual.length !== report.totals?.total) failures.push(`${prefix}: duplicate IDs or inconsistent case counts.`);
    for (const id of specification.requiredIds) {
      if (!actual.some(entry => entry.id === id && entry.status === 'pass')) failures.push(`${prefix}: required compiler case did not pass: ${id}`);
    }
    if (actual.some(entry => entry.status !== 'pass')) failures.push(`${prefix}: compiler failures or skipped cases remain.`);
  } else {
    if (report.run?.backend !== specification.backend || report.run?.platform !== specification.platform) failures.push(`${prefix}: platform/backend mismatch.`);
    if (specification.nativeVulkan && (report.run?.platform === 'darwin' || report.evidence?.gpuInventory?.vulkanEnvironment === 'macos-portability')) failures.push(`${prefix}: portability Vulkan cannot prove native Vulkan support.`);
    const targets = report.targets ?? [];
    if (new Set(targets.map(entry => entry.target)).size !== targets.length) failures.push(`${prefix}: duplicate target IDs.`);
    for (const id of specification.requiredIds) {
      const target = targets.find(entry => entry.target === id);
      if (!target || target.exitCode !== 0 || !(target.executionCounts?.executed > 0) || target.reportError) failures.push(`${prefix}: required backend target did not execute successfully: ${id}`);
    }
    const allowed = new Set(specification.allowedSkips ?? []);
    for (const entry of report.cases ?? []) {
      const id = `${entry.target?.target}/${entry.fullName}`;
      if (entry.status === 'skip' && !allowed.has(id)) failures.push(`${prefix}: skip not declared before validation: ${id}`);
      if (entry.status === 'fail') failures.push(`${prefix}: failed backend case: ${id}`);
    }
    for (const step of report.steps ?? []) if (step.exitCode !== 0) failures.push(`${prefix}: ${step.name} failed.`);
  }
}

/** Reads evidence relative to a review manifest, preserving missing files as explicit audit failures. */
async function readEvidence(base, file, failures) {
  if (typeof file !== 'string' || !file) { failures.push('Missing evidence path.'); return null; }
  try { return JSON.parse(await fs.readFile(path.resolve(base, file), 'utf8')); }
  catch (error) { failures.push(`${file}: ${error.message}`); return null; }
}

/** Verifies three distinct complete rounds per requested scope and records all remaining promotion prerequisites. */
export async function auditCandidate(manifest, base) {
  const failures = [];
  if (!/^[a-f0-9]{64}$/u.test(manifest.sourceSha256 ?? '')) failures.push('A candidate source SHA-256 is required.');
  if (manifest.defaultPipeline !== 'UGLIR') failures.push('The report must identify the UGLIR default pipeline.');
  const inventory = await readEvidence(base, manifest.capabilityInventory, failures);
  if (inventory?.formalContractComplete !== true || inventory?.blockingGaps?.length !== 0) failures.push('The complete formal capability contract still has unaudited or unsupported features.');
  if (!Array.isArray(manifest.scopes) || manifest.scopes.length === 0) failures.push('No proposed platform/function scope was supplied.');
  for (let index = 1; index <= 17; ++index) {
    const id = `T${String(index).padStart(2, '0')}`;
    const task = manifest.tasks?.find(entry => entry.id === id);
    if (task?.verified !== true || !task.evidence?.length) failures.push(`${id}: required acceptance evidence is incomplete.`);
    for (const file of task?.evidence ?? []) await readEvidence(base, file, failures);
  }
  const usedRuns = new Set();
  const compilerByScope = new Map();
  for (const scope of manifest.scopes ?? []) {
    if (!Array.isArray(scope.rounds) || scope.rounds.length < 3) failures.push(`${scope.id}: three complete consecutive rounds are required.`);
    let previousCompletion = 0;
    for (const [roundIndex, round] of (scope.rounds ?? []).entries()) {
      let firstStart = Infinity;
      let lastCompletion = 0;
      for (const [kind, experimental, key] of [['uglc', false, 'uglcOff'], ['uglc', true, 'uglcOn'], ['gvm', false, 'gvmOff'], ['gvm', true, 'gvmOn']]) {
        const report = await readEvidence(base, round[key], failures);
        if (!report) continue;
        const id = `${scope.id}/${roundIndex + 1}/${key}`;
        const identity = `${scope.id}/${key}/${report.run?.runId}`;
        if (usedRuns.has(identity)) failures.push(`${id}: a prior run was reused as a new validation round.`);
        usedRuns.add(identity);
        auditNodeReport(report, { ...scope, id, kind, experimental, sourceSha256: manifest.sourceSha256,
          requiredIds: scope.requiredIds?.[key], allowedSkips: scope.allowedSkips?.[key] }, failures);
        const compilerKey = `${scope.id}/${experimental}`;
        const digest = report.evidence?.compiler?.sha256;
        if (compilerByScope.has(compilerKey) && compilerByScope.get(compilerKey) !== digest) failures.push(`${id}: compiler binary changed between reports.`);
        compilerByScope.set(compilerKey, digest);
        firstStart = Math.min(firstStart, Date.parse(report.run?.startedAt));
        lastCompletion = Math.max(lastCompletion, Date.parse(report.run?.completedAt));
        if (kind === 'gvm') {
          for (const raw of report.rawReports ?? []) {
            const rawPath = path.join(path.dirname(path.resolve(base, round[key])), raw.filePath);
            try { inspectGoogleTestReport(JSON.parse(await fs.readFile(rawPath, 'utf8'))); }
            catch (error) { failures.push(`${id}: invalid raw report ${rawPath}: ${error.message}`); }
          }
          if (!report.rawReports?.length) failures.push(`${id}: raw GoogleTest reports are missing.`);
        }
      }
      if (!Number.isFinite(firstStart) || !Number.isFinite(lastCompletion) || firstStart < previousCompletion || lastCompletion < firstStart) failures.push(`${scope.id}: rounds must be complete, ordered and non-overlapping.`);
      previousCompletion = lastCompletion;
    }
    for (const [key, kind] of [['performance', 'paired-performance'], ['sustained', 'sustained-readback']]) {
      if (key === 'performance' && !scope.performance) continue;
      const report = await readEvidence(base, scope[key], failures);
      if (report?.kind !== kind || report?.passed !== true || report?.backend !== scope.backend || report?.evidence?.sourceSha256 !== manifest.sourceSha256) failures.push(`${scope.id}: ${key} has not passed for this candidate.`);
      try {
        if (key === 'performance') {
          if (!Object.keys(report?.fixtures ?? {}).length) throw new Error('No performance fixtures were measured.');
          for (const fixture of Object.values(report.fixtures)) {
            comparePairedMeasurements(fixture.pairs);
            if (fixture.pairs.some(pair => !pair.legacy.embeddedPayloadsVerified || !pair.experimental.embeddedPayloadsVerified)) throw new Error('Measured shader payloads were not verified against the executing binaries.');
          }
        } else {
          if (report?.measured?.exitCode !== 0 || report?.measured?.timedOut || !Number.isInteger(report?.seconds) || report.seconds < 1800) throw new Error('The sustained run failed or did not request the required duration.');
          const raw = await readEvidence(path.dirname(path.resolve(base, scope[key])), 'readback.json', failures);
          inspectReadbackReport(raw, scope.backend, report.seconds * 1000);
        }
      } catch (error) { failures.push(`${scope.id}: ${key}: ${error.message}`); }
    }
    const sceneReport = await readEvidence(base, scope.scenes, failures);
    if (sceneReport?.status !== 'pass' || !sceneReport?.quadrants?.length || sceneReport?.generatedArtifacts?.status !== 'pass') failures.push(`${scope.id}: frozen representative scene evidence is incomplete.`);
  }
  return { schemaVersion: 1, legacyEnabled: true, defaultPipeline: 'UGLIR', switchAuthorized: false,
    eligibleForFormalizationReview: failures.length === 0, sourceSha256: manifest.sourceSha256 ?? null, failures };
}

/** Audits an explicitly supplied candidate manifest; successful preparation never changes a build or runtime default. */
async function main() {
  const args = process.argv.slice(2);
  const options = {};
  for (let index = 0; index < args.length; index += 2) options[args[index]?.replace(/^--/u, '')] = args[index + 1];
  if (!options.manifest || !options.output) throw new Error('Specify --manifest <candidate.json> and --output <audit.json>.');
  const manifestPath = path.resolve(options.manifest);
  const result = await auditCandidate(JSON.parse(await fs.readFile(manifestPath, 'utf8')), path.dirname(manifestPath));
  await fs.mkdir(path.dirname(path.resolve(options.output)), { recursive: true });
  await fs.writeFile(options.output, JSON.stringify(result, null, 2) + '\n');
  console.log(`Candidate review eligible: ${result.eligibleForFormalizationReview}; ${result.failures.length} unmet condition(s). Defaults remain unchanged.`);
  process.exitCode = result.eligibleForFormalizationReview ? 0 : 1;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) await main();
