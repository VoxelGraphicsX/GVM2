#!/usr/bin/env node
import { spawn, execFile } from 'node:child_process';
import { promises as fs, createWriteStream } from 'node:fs';
import { promisify } from 'node:util';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { performance } from 'node:perf_hooks';
import { captureCompilerEvidence, hashFile, verifyUnchangedCandidate, verifyEmbeddedComputePayloads } from '../../shared/compiler-evidence.mjs';
import { inspectReadbackReport } from '../../node/execution-contract.mjs';
import { getProfile } from './test-config.mjs';
import { comparePairedMeasurements, inspectMemoryTrend } from './readiness-metrics.mjs';

const sourceDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const executeFile = promisify(execFile);

/** Parses explicit readiness commands; runtime configuration never comes from environment variables. */
function parseArguments() {
  const [command, ...args] = process.argv.slice(2);
  const options = {};
  for (let index = 0; index < args.length; index += 2) {
    if (!args[index].startsWith('--') || !args[index + 1] || args[index + 1].startsWith('--')) throw new Error('Readiness options require explicit values.');
    options[args[index].slice(2)] = args[index + 1];
  }
  if (!['sustain', 'measure'].includes(command)) throw new Error('Expected sustain or measure.');
  if (!options['build-dir'] || !options.output || !['metal', 'vulkan'].includes(options.backend)) throw new Error('Specify --build-dir, --backend metal|vulkan and --output.');
  return { command, options };
}

/** Runs one executable while preserving its log, process result, and sampled resident memory. */
async function executeMeasured(command, args, outputPrefix, timeoutSeconds) {
  const started = performance.now();
  const log = createWriteStream(`${outputPrefix}.log`, { flags: 'wx' });
  const child = spawn(command, args, { cwd: sourceDir, stdio: ['ignore', 'pipe', 'pipe'] });
  child.stdout.pipe(log, { end: false });
  child.stderr.pipe(log, { end: false });
  let finished = false;
  let timedOut = false;
  const samples = [];
  const timer = setTimeout(() => { timedOut = true; child.kill('SIGKILL'); }, timeoutSeconds * 1000);
  const completion = new Promise(resolve => {
    child.on('error', error => { finished = true; resolve({ exitCode: 127, error: error.message }); });
    child.on('close', (code, signal) => { finished = true; resolve({ exitCode: code ?? 1, signal }); });
  });
  while (!finished) {
    try {
      const { stdout } = await executeFile('ps', ['-o', 'rss=', '-p', String(child.pid)], { timeout: 2000 });
      const rssBytes = Number(stdout.trim()) * 1024;
      if (Number.isFinite(rssBytes) && rssBytes > 0) samples.push({ seconds: (performance.now() - started) / 1000, rssBytes });
    } catch { /* A process may exit between the completion check and the RSS query. */ }
    if (!finished) await Promise.race([completion, new Promise(resolve => setTimeout(resolve, 1000))]);
  }
  const result = await completion;
  clearTimeout(timer);
  await new Promise(resolve => log.end(resolve));
  await fs.writeFile(`${outputPrefix}.rss.json`, JSON.stringify(samples, null, 2));
  return { ...result, timedOut, durationMs: performance.now() - started, samples, command: [command, ...args] };
}

/** Executes at least 30 minutes of correct readback, rejects failures or candidate changes, and records optional RSS observations. */
async function sustain(options, buildDir, output, evidence) {
  const seconds = Number(options['duration-seconds'] ?? 1800);
  if (!Number.isInteger(seconds) || seconds < 1800 || seconds > 86400) throw new Error('Sustained duration must be 1800..86400 seconds.');
  const fixture = options.fixture ?? 'evaluation';
  if (!['evaluation', 'buffer_layout'].includes(fixture)) throw new Error('Fixture must be evaluation or buffer_layout.');
  const original = path.join(buildDir, 'gvm_tests/bin', `gvm_rhi_uglir_${fixture}_semantics_tests`);
  const executable = path.join(output, 'readback-executable');
  await fs.copyFile(original, executable, fs.constants.COPYFILE_EXCL);
  const binarySha256 = await hashFile(executable);
  const reportPath = path.join(output, 'readback.json');
  const measured = await executeMeasured(executable, ['--backend', options.backend, '--readback-duration-seconds', String(seconds), `--gtest_output=json:${reportPath}`], path.join(output, 'readback'), seconds + 120);
  const failures = [];
  let memory = null;
  try {
    if (measured.exitCode !== 0 || measured.timedOut) throw new Error('Sustained readback failed or timed out.');
    inspectReadbackReport(JSON.parse(await fs.readFile(reportPath, 'utf8')), options.backend, seconds * 1000);
    try { memory = inspectMemoryTrend(measured.samples, seconds); }
    catch (error) { memory = { unavailable: error.message }; }
    verifyUnchangedCandidate(evidence, await captureCompilerEvidence(sourceDir, buildDir));
    if (binarySha256 !== await hashFile(original)) failures.push('The current readback executable differs from the tested copy.');
  } catch (error) { failures.push(error.message); }
  return { kind: 'sustained-readback', backend: options.backend, fixture, seconds, binarySha256, evidence, measured, memory, failures, passed: failures.length === 0 };
}

/** Parses the platform process peak RSS supplied by the operating system's time utility. */
async function readPeakRss(logPath) {
  const text = await fs.readFile(logPath, 'utf8');
  const match = process.platform === 'darwin' ? /(\d+)\s+maximum resident set size/u.exec(text) : /Maximum resident set size \(kbytes\):\s*(\d+)/u.exec(text);
  if (!match) throw new Error('The operating system did not supply peak RSS evidence.');
  return Number(match[1]) * (process.platform === 'darwin' ? 1 : 1024);
}

/** Compiles the same source and executes its matching readback binary for one half of a paired measurement. */
async function measurePipeline(profile, fixture, pipeline, backend, outputPrefix) {
  const generated = `${outputPrefix}-generated`;
  await fs.mkdir(generated);
  const source = path.join(sourceDir, 'tests/rhi_cases', fixture === 'evaluation' ? 'EvaluationSemantics.hpp' : 'BufferLayoutSemantics.hpp');
  const args = [profile.uglcExecutable, '-s', source, '-o', generated,
    ...profile.uglcIncludeDirs.map(directory => `-I${directory}`),
    ...(profile.clangResourceDir ? ['--resource-dir', profile.clangResourceDir] : []),
    ...(pipeline === 'uglir' ? ['--shader-pipeline=uglir'] : [])];
  const compile = await executeMeasured('/usr/bin/time', [process.platform === 'darwin' ? '-l' : '-v', ...args], `${outputPrefix}-compile`, 180);
  if (compile.exitCode !== 0) throw new Error(`Compiler failed: ${outputPrefix}-compile.log`);
  const peakRssBytes = await readPeakRss(`${outputPrefix}-compile.log`);
  const binary = path.join(profile.buildDir, 'gvm_tests/bin', `gvm_rhi_${pipeline === 'uglir' ? 'uglir' : 'legacy'}_${fixture}_semantics_tests`);
  const payloads = verifyEmbeddedComputePayloads(await fs.readFile(path.join(generated, 'generate_result.hpp'), 'utf8'), await fs.readFile(binary));
  const reportPath = `${outputPrefix}-gpu.json`;
  const execution = await executeMeasured(binary, ['--backend', backend, '--measure-gpu-time', `--gtest_output=json:${reportPath}`], `${outputPrefix}-gpu`, 180);
  if (execution.exitCode !== 0) throw new Error(`GPU measurement failed: ${outputPrefix}-gpu.log`);
  const [entry] = inspectReadbackReport(JSON.parse(await fs.readFile(reportPath, 'utf8')), backend);
  return { ...payloads, compileMs: compile.durationMs, peakRssBytes, computeClassCreationUs: Number(entry.compute_class_creation_us),
    gpuPassNs: Number(entry.mean_gpu_pass_ns), hostArtifactBytes: (await fs.stat(path.join(generated, 'generate_result.hpp'))).size,
    executableSha256: await hashFile(binary), compileCommand: compile.command, gpuCommand: execution.command };
}

/** Collects at least five alternating-order pairs without treating small fixtures as a full representative workload. */
async function measure(options, buildDir, output, evidence) {
  const count = Number(options.pairs ?? 5);
  if (!Number.isInteger(count) || count < 5) throw new Error('At least five pairs are required.');
  const profile = getProfile(sourceDir, 'gvm-local', { buildDirOverride: buildDir });
  const fixtures = {};
  for (const fixture of ['evaluation', 'buffer_layout']) {
    const pairs = [];
    for (let index = 0; index < count; ++index) {
      const pair = {};
      for (const pipeline of index % 2 ? ['uglir', 'legacy'] : ['legacy', 'uglir']) {
        pair[pipeline] = await measurePipeline(profile, fixture, pipeline, options.backend, path.join(output, `${fixture}-${index}-${pipeline}`));
      }
      pairs.push(pair);
      console.log(`Measured ${fixture} pair ${index + 1}/${count}.`);
    }
    fixtures[fixture] = { pairs, ...comparePairedMeasurements(pairs) };
  }
  verifyUnchangedCandidate(evidence, await captureCompilerEvidence(sourceDir, buildDir));
  return { kind: 'paired-performance', backend: options.backend, evidence, fixtures,
    scope: 'Optional observations from two semantic fixtures: whole UGLC invocation, process peak RSS, combined compute-class creation and GPU timestamps. Passed means measurement and readback succeeded, not that performance was certified.',
    passed: true };
}

/** Runs explicit readiness measurements and always preserves a machine-readable failure report. */
async function main() {
  const { command, options } = parseArguments();
  const buildDir = path.resolve(options['build-dir']);
  const output = path.resolve(options.output);
  await fs.mkdir(path.dirname(output), { recursive: true });
  await fs.mkdir(output);
  let report;
  try {
    const evidence = await captureCompilerEvidence(sourceDir, buildDir);
    report = command === 'sustain' ? await sustain(options, buildDir, output, evidence) : await measure(options, buildDir, output, evidence);
  } catch (error) { report = { kind: command, passed: false, error: error.message }; }
  report.pipeline = 'UGLIR';
  report.defaultPipeline = 'UGLIR';
  await fs.writeFile(path.join(output, 'summary.json'), JSON.stringify(report, null, 2));
  console.log(`Readiness report: ${path.join(output, 'summary.json')}`);
  process.exitCode = report.passed ? 0 : 1;
}

await main();
