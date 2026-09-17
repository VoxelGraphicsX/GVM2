import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { parseOutputPolicy, RunOutput, RotatingLog, listReports, runLoggedCommand } from './output-policy.mjs';

/** Provides an isolated output root and removes all test-owned data afterwards. */
function outputRoot(context) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'gvm-output-policy-'));
  context.after(() => fs.rmSync(root, { recursive: true, force: true }));
  return root;
}

/** Writes a small report with a deliberately controlled execution time. */
function writeReport(output, runId, startedAt) {
  fs.writeFileSync(path.join(output.runDir, 'summary.json'), JSON.stringify({ run: { runId, startedAt } }));
}

test('defaults retain images, overwrite reports and bound rotating logs', () => {
  const policy = parseOutputPolicy({});
  assert.equal(policy.writeImages, true);
  assert.equal(policy.outputMode, 'overwrite');
  assert.equal(policy.logMode, 'rotate');
  assert.equal(policy.keepRuns, 2);
  assert.equal(policy.keepLogRuns, 2);
  assert.equal(policy.logMaxBytes, 10 * 1024 * 1024);
  assert.equal(policy.logBackups, 2);
  for (const options of [
    { 'write-images': 'maybe' }, { 'output-mode': '../' }, { 'log-mode': 'append' },
    { 'keep-runs': '0' }, { 'keep-log-runs': '-1' }, { 'log-max-mib': '11' }, { 'log-backups': 'Infinity' }
  ]) assert.throws(() => parseOutputPolicy(options));
});

test('overwrite removes stale images but preserves bounded previous log segments', context => {
  const root = outputRoot(context);
  const policy = parseOutputPolicy({});
  const first = new RunOutput(root, 'first', policy);
  const logFile = path.join(first.logDir, 'render.log');
  fs.mkdirSync(path.join(first.runDir, 'artifacts'));
  fs.writeFileSync(path.join(first.runDir, 'artifacts', 'old.rgba'), 'pixels');
  const log = new RotatingLog(logFile, policy);
  log.write('first log'); log.close();
  first.close();
  const second = new RunOutput(root, 'second', policy);
  try {
    assert.equal(second.runDir, first.runDir);
    assert.equal(fs.existsSync(path.join(second.runDir, 'artifacts', 'old.rgba')), false);
    const next = new RotatingLog(logFile, policy);
    next.write('second log'); next.close();
    assert.equal(fs.readFileSync(logFile, 'utf8'), 'second log');
    assert.equal(fs.readFileSync(`${logFile}.1`, 'utf8'), 'first log');
  } finally { second.close(); }
});

test('timestamp report retention never deletes unrelated directories or breaks retained log paths', context => {
  const root = outputRoot(context);
  fs.mkdirSync(path.join(root, 'old-unmanaged-report'));
  fs.writeFileSync(path.join(root, 'old-unmanaged-report', 'keep.txt'), 'keep');
  const policy = parseOutputPolicy({ 'output-mode': 'timestamp' });
  for (const runId of ['a', 'b', 'c']) {
    const output = new RunOutput(root, runId, policy);
    fs.writeFileSync(path.join(output.logDir, 'test.log'), runId);
    output.close();
  }
  assert.equal(fs.existsSync(path.join(root, 'a')), false);
  for (const id of ['b', 'c']) assert.equal(fs.readFileSync(path.join(root, id, 'logs/current/test.log'), 'utf8'), id);
  assert.equal(fs.readFileSync(path.join(root, 'old-unmanaged-report/keep.txt'), 'utf8'), 'keep');
});

test('timestamped logs are retained independently when reports are overwritten', context => {
  const root = outputRoot(context);
  const policy = parseOutputPolicy({ 'log-mode': 'timestamp' });
  for (const runId of ['a', 'b', 'c']) {
    const output = new RunOutput(root, runId, policy);
    fs.writeFileSync(path.join(output.logDir, 'test.log'), runId);
    output.close();
  }
  assert.deepEqual(fs.readdirSync(path.join(root, 'latest/logs')).sort(), ['b', 'c']);
});

test('one oversized write stays bounded and retains the newest bytes in order', context => {
  const root = outputRoot(context);
  const file = path.join(root, 'test.log');
  const policy = { logMaxBytes: 8, logBackups: 2 };
  const log = new RotatingLog(file, policy);
  log.write('0123456789abcdefghijklmnopqrstuvwxyz'); log.close();
  const files = [`${file}.2`, `${file}.1`, file];
  assert.ok(files.every(entry => fs.statSync(entry).size <= 8));
  assert.equal(files.map(entry => fs.readFileSync(entry, 'utf8')).join(''), 'ghijklmnopqrstuvwxyz');
  assert.equal(fs.existsSync(`${file}.3`), false);
  const append = new RotatingLog(file, policy, true);
  append.write('END'); append.close();
  assert.equal(fs.readFileSync(file, 'utf8'), 'wxyzEND');
});

test('locking prevents competing cleanup and releases after a rejected unowned directory', context => {
  const root = outputRoot(context);
  const policy = parseOutputPolicy({});
  const first = new RunOutput(root, 'first', policy);
  try {
    fs.writeFileSync(path.join(first.runDir, 'keep'), 'active');
    assert.throws(() => new RunOutput(root, 'second', policy), /Another runner is active/);
    assert.equal(fs.readFileSync(path.join(first.runDir, 'keep'), 'utf8'), 'active');
  } finally { first.close(); }
  fs.rmSync(path.join(root, 'latest/.runner-output.json'));
  assert.throws(() => new RunOutput(root, 'third', policy), /unowned report/);
  assert.equal(fs.existsSync(path.join(root, '.runner.lock')), false);
});

test('lowering log limits trims existing segments and removes excess backups', context => {
  const root = outputRoot(context);
  const file = path.join(root, 'large.log');
  for (const suffix of ['', '.1', '.2']) fs.writeFileSync(file + suffix, '0123456789');
  const log = new RotatingLog(file, { logMaxBytes: 4, logBackups: 1 }, true);
  log.write('end'); log.close();
  assert.equal(fs.existsSync(`${file}.2`), false);
  assert.equal(fs.readFileSync(`${file}.1`, 'utf8'), '6789');
  assert.equal(fs.readFileSync(file, 'utf8'), 'end');
});

test('latest report discovery uses execution time rather than lexicographic folder order', context => {
  const root = outputRoot(context);
  const first = new RunOutput(root, 'first', parseOutputPolicy({}));
  writeReport(first, 'first', '2020-01-01T00:00:00Z'); first.close();
  const next = new RunOutput(root, '20260101', parseOutputPolicy({ 'output-mode': 'timestamp' }));
  writeReport(next, 'second', '2026-01-01T00:00:00Z'); next.close();
  assert.deepEqual(listReports(root).map(entry => entry.name), ['20260101', 'latest']);
});

test('streamed logs preserve redaction and the actual process exit code', async context => {
  const root = outputRoot(context);
  const output = new RunOutput(root, 'first', parseOutputPolicy({}));
  try {
    const logPath = path.join(output.logDir, 'process.log');
    const result = await runLoggedCommand(process.execPath, ['-e', 'console.log("private-path"); process.exitCode = 7;'],
      { logPath, redact: text => text.replaceAll('private-path', '<path>') }, output);
    assert.equal(result.exitCode, 7);
    assert.match(result.output, /private-path/);
    assert.equal(fs.readFileSync(logPath, 'utf8'), '<path>\n');
  } finally { output.close(); }
});

test('a closed test window cancels subsequent commands instead of opening another window', async context => {
  const root = outputRoot(context);
  const output = new RunOutput(root, 'first', parseOutputPolicy({}));
  try {
    const options = { logPath: path.join(output.logDir, 'closed.log') };
    const result = await runLoggedCommand(process.execPath,
      ['-e', 'console.log("Window was closed before render test case finished"); setInterval(() => {}, 1000);'], options, output);
    assert.equal(result.exitCode, 130);
    assert.equal(output.controller.signal.aborted, true);
    await assert.rejects(runLoggedCommand(process.execPath, ['-e', 'process.exit(0)'], options, output), /window was closed/);
  } finally { output.close(); }
});
