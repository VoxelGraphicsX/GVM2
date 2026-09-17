import test from 'node:test';
import assert from 'node:assert/strict';
import { promises as fs } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { auditNodeReport, auditCandidate } from './candidate-audit.mjs';
import { verifyEmbeddedComputePayloads } from '../../shared/compiler-evidence.mjs';

/** Supplies a minimal complete compiler report for testing evidence rejection, independently of registry implementation. */
function compilerReport() {
  return { run: { group: 'all', exitCode: 0, legacyEnabled: true },
    evidence: { sourceSha256: 'a'.repeat(64), compiler: { sha256: 'b'.repeat(64) }, gpuInventory: { available: true } },
    totals: { total: 1, pass: 1, fail: 0, skip: 0 }, cases: [{ id: 'required-case', status: 'pass' }] };
}

const specification = { id: 'compiler-on', kind: 'uglc', legacyEnabled: true, sourceSha256: 'a'.repeat(64), requiredIds: ['required-case'] };

test('candidate auditing rejects partial selection, missing IDs and mixed source versions', () => {
  const accepted = [];
  auditNodeReport(compilerReport(), specification, accepted);
  assert.deepEqual(accepted, []);
  for (const report of [
    { ...compilerReport(), run: { ...compilerReport().run, group: 'uglir' } },
    { ...compilerReport(), cases: [] },
    { ...compilerReport(), evidenceError: 'source changed' },
    { ...compilerReport(), evidence: { ...compilerReport().evidence, sourceSha256: 'c'.repeat(64) } }
  ]) {
    const failures = [];
    auditNodeReport(report, specification, failures);
    assert.ok(failures.length > 0);
  }
});

test('preparation evidence never authorizes changing the default pipeline', async () => {
  const result = await auditCandidate({ legacyEnabled: true, defaultPipeline: 'UGLIR', scopes: [] }, process.cwd());
  assert.equal(result.eligibleForFormalizationReview, false);
  assert.equal(result.switchAuthorized, false);
  assert.ok(result.failures.some(message => message.includes('T17')));
});

test('performance evidence must match actual embedded runtime bytes', () => {
  const header = 'static const eastl::string __UGL__Global__MSLHeader = R"(prelude)";\n' +
    'static constexpr uint32_t computeShaderArtifact_SpirvWords[] = {0x07230203,0x00010300,0x0,0x1,0x0};\n' +
    'const auto computeShaderArtifact = UGLC::Generated::MakeShaderArtifact(__UGL__Global__MSLHeader + R"(kernel body)");';
  const words = Buffer.alloc(20);
  [0x07230203, 0x00010300, 0, 1, 0].forEach((word, index) => words.writeUInt32LE(word, index * 4));
  const binary = Buffer.concat([Buffer.from('prelude\0kernel body\0'), words]);
  assert.equal(verifyEmbeddedComputePayloads(header, binary).spirvBytes, 20);
  assert.throws(() => verifyEmbeddedComputePayloads(header, Buffer.from('old shader executable')), /rebuild/u);
  assert.throws(() => verifyEmbeddedComputePayloads('', binary), /missing/u);
});

test('candidate review treats performance as optional and validates actual sustained readback', async (t) => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'uglc-audit-observations-'));
  t.after(() => fs.rm(directory, { recursive: true, force: true }));
  const sourceSha256 = 'a'.repeat(64);
  const scope = { id: 'metal', backend: 'metal', rounds: [], sustained: 'sustained.json' };
  const manifest = { sourceSha256, legacyEnabled: true, defaultPipeline: 'UGLIR', scopes: [scope] };
  const sustained = { kind: 'sustained-readback', passed: true, backend: 'metal', evidence: { sourceSha256 },
    seconds: 1800, measured: { exitCode: 0, timedOut: false }, memory: { windowDeltaBytes: 163840, bytesPerMinute: 6711.82 } };
  const entry = { status: 'RUN', result: 'COMPLETED', backend: 'metal', readback_duration_ms: '1800000', readback_iterations: '3000000' };
  const raw = { tests: 1, testsuites: [{ testsuite: [entry] }] };
  await fs.writeFile(path.join(directory, 'sustained.json'), JSON.stringify(sustained));
  await fs.writeFile(path.join(directory, 'readback.json'), JSON.stringify(raw));
  let result = await auditCandidate(manifest, directory);
  assert.equal(result.failures.some(message => /^metal: (performance|sustained)/u.test(message)), false);
  const legacy = { compileMs: 100, peakRssBytes: 1000, computeClassCreationUs: 100, gpuPassNs: 100, embeddedPayloadsVerified: true };
  const experimental = { ...legacy, compileMs: 125, gpuPassNs: 114 };
  const performance = { kind: 'paired-performance', passed: true, backend: 'metal', evidence: { sourceSha256 },
    fixtures: { evaluation: { pairs: Array.from({ length: 5 }, () => ({ legacy, experimental })) } } };
  scope.performance = 'performance.json';
  await fs.writeFile(path.join(directory, 'performance.json'), JSON.stringify(performance));
  result = await auditCandidate(manifest, directory);
  assert.equal(result.failures.some(message => /^metal: (performance|sustained)/u.test(message)), false);
  delete entry.readback_duration_ms;
  await fs.writeFile(path.join(directory, 'readback.json'), JSON.stringify(raw));
  result = await auditCandidate(manifest, directory);
  assert.ok(result.failures.some(message => /^metal: sustained: Readback did not execute/u.test(message)));
  assert.equal(result.switchAuthorized, false);
});
