import fs from 'node:fs';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { RedactedText } from './report-redaction.mjs';

const MiB = 1024 * 1024;
const markerName = '.runner-output.json';

/** Parses a bounded positive command-line integer before any output is removed. */
function positiveInteger(options, name, fallback, maximum) {
  const value = options[name] ?? String(fallback);
  if (!/^\d+$/u.test(value) || Number(value) < 1 || Number(value) > maximum) {
    throw new Error(`--${name} must be an integer between 1 and ${maximum}.`);
  }
  return Number(value);
}

/** Validates the independent report, image and log retention settings. */
export function parseOutputPolicy(options) {
  const outputMode = options['output-mode'] ?? 'overwrite';
  const logMode = options['log-mode'] ?? 'rotate';
  const writeImages = options['write-images'] ?? 'true';
  if (!['overwrite', 'timestamp'].includes(outputMode)) throw new Error('--output-mode must be overwrite or timestamp.');
  if (!['rotate', 'timestamp'].includes(logMode)) throw new Error('--log-mode must be rotate or timestamp.');
  if (!['true', 'false'].includes(writeImages)) throw new Error('--write-images must be true or false.');
  return {
    outputMode, logMode, writeImages: writeImages === 'true',
    keepRuns: positiveInteger(options, 'keep-runs', 2, 20),
    keepLogRuns: positiveInteger(options, 'keep-log-runs', 2, 20),
    logMaxBytes: positiveInteger(options, 'log-max-mib', 10, 10) * MiB,
    logBackups: positiveInteger(options, 'log-backups', 2, 2)
  };
}

/** Documents the common CLI switches and their finite default limits. */
export const outputPolicyHelp = `Output options:
  --open-report true|false        Explicitly open the report after testing (default: false).
  GVM render tests use hidden background windows and never raise them.
  --write-images true|false       Write raw RGBA and PPM images (default: true; GVM only).
                                 false skips file-based Legacy/UGLIR pixel comparison.
  --output-mode overwrite|timestamp  Reuse runs/latest or create a new run (default: overwrite).
  --log-mode rotate|timestamp     Rotate logs in place or group them by run (default: rotate).
  --keep-runs N                   Retain at most N managed report directories (default: 2).
  --keep-log-runs N               Retain N timestamped log groups per report (default: 2).
  --log-max-mib N                 Maximum size per text log, 1..10 MiB (default: 10).
  --log-backups N                 Rotated backups per log, 1..2 (default: 2).
  Runs stop below 10 GiB free, above 1 GiB per report or 2 GiB of managed reports.
  Existing report directories without a retention marker are left untouched.`;

/** Lists valid reports by their recorded time, including the reusable latest directory. */
export function listReports(runsRoot) {
  if (!fs.existsSync(runsRoot)) return [];
  const reports = [];
  for (const entry of fs.readdirSync(runsRoot, { withFileTypes: true })) {
    if (!entry.isDirectory()) continue;
    try {
      const summary = JSON.parse(fs.readFileSync(path.join(runsRoot, entry.name, 'summary.json'), 'utf8'));
      const time = Date.parse(summary.run?.startedAt);
      if (Number.isFinite(time)) reports.push({ name: entry.name, summary, time });
    } catch { /* Ignore incomplete or malformed reports. */ }
  }
  return reports.sort((left, right) => right.time - left.time);
}

/** Measures owned output without traversing symbolic links. */
function directoryBytes(directory) {
  let bytes = 0;
  let entries;
  try { entries = fs.readdirSync(directory, { withFileTypes: true }); }
  catch (error) { if (error.code === 'ENOENT') return 0; throw error; }
  for (const entry of entries) {
    const file = path.join(directory, entry.name);
    if (entry.isDirectory()) bytes += directoryBytes(file);
    else if (entry.isFile()) {
      try { bytes += fs.statSync(file).size; }
      catch (error) { if (error.code !== 'ENOENT') throw error; }
    }
  }
  return bytes;
}

/** Lists only directories created by this retention implementation. */
function managedDirectories(root) {
  if (!fs.existsSync(root)) return [];
  return fs.readdirSync(root, { withFileTypes: true }).filter(entry => entry.isDirectory()
    && fs.existsSync(path.join(root, entry.name, markerName)))
    .map(entry => path.join(root, entry.name))
    .sort((left, right) => fs.statSync(path.join(right, markerName)).mtimeMs - fs.statSync(path.join(left, markerName)).mtimeMs);
}

/** Removes superseded managed directories while preserving the active output. */
function pruneDirectories(root, current, keep) {
  const previous = managedDirectories(root).filter(directory => directory !== current);
  for (const directory of previous.slice(keep - 1)) fs.rmSync(directory, { recursive: true, force: true });
}

/** Owns one report directory, exclusive runner lock, cancellation and disk budget. */
export class RunOutput {
  /** Acquires exclusive ownership before clearing or pruning report output. */
  constructor(runsRoot, runId, policy) {
    this.policy = policy;
    this.runsRoot = path.resolve(runsRoot);
    this.runDir = path.join(this.runsRoot, policy.outputMode === 'overwrite' ? 'latest' : runId);
    this.controller = new AbortController();
    this.lockPath = path.join(this.runsRoot, '.runner.lock');
    this.stopHandler = () => this.cancel('Test run interrupted by the user.');
    fs.mkdirSync(this.runsRoot, { recursive: true });
    try {
      const owner = Number(fs.readFileSync(this.lockPath, 'utf8'));
      if (!Number.isSafeInteger(owner) || owner <= 0) throw new Error(`Invalid runner lock: ${this.lockPath}`);
      try { process.kill(owner, 0); }
      catch (error) {
        if (error.code !== 'ESRCH') throw error;
        fs.unlinkSync(this.lockPath);
      }
      if (fs.existsSync(this.lockPath)) throw new Error(`Another runner is active (PID ${owner}) under ${this.runsRoot}.`);
    } catch (error) { if (error.code !== 'ENOENT') throw error; }
    const lock = fs.openSync(this.lockPath, 'wx');
    fs.writeFileSync(lock, String(process.pid));
    fs.closeSync(lock);
    this.locked = true;
    try {
      const disk = fs.statfsSync(this.runsRoot);
      if (disk.bavail * disk.bsize < 10 * 1024 ** 3) throw new Error('Less than 10 GiB free; refusing to start a test run.');
      if (fs.existsSync(this.runDir)) {
        if (policy.outputMode === 'timestamp') throw new Error(`Timestamped report already exists: ${this.runDir}`);
        if (fs.lstatSync(this.runDir).isSymbolicLink() || !fs.existsSync(path.join(this.runDir, markerName))) {
          throw new Error(`Refusing to overwrite an unowned report directory: ${this.runDir}`);
        }
        for (const entry of fs.readdirSync(this.runDir)) {
          if (entry !== 'logs') fs.rmSync(path.join(this.runDir, entry), { recursive: true, force: true });
        }
      }
      fs.mkdirSync(this.runDir, { recursive: true });
      fs.writeFileSync(path.join(this.runDir, markerName), JSON.stringify({ runId, policy }));
      pruneDirectories(this.runsRoot, this.runDir, policy.keepRuns);
      const logsRoot = path.join(this.runDir, 'logs');
      this.logDir = path.join(logsRoot, policy.logMode === 'rotate' ? 'current' : runId);
      fs.mkdirSync(this.logDir, { recursive: true });
      fs.writeFileSync(path.join(this.logDir, markerName), JSON.stringify({ runId }));
      pruneDirectories(logsRoot, this.logDir, policy.keepLogRuns);
      this.checkBudget();
      process.on('SIGINT', this.stopHandler);
      process.on('SIGTERM', this.stopHandler);
      this.monitor = setInterval(() => {
        try { this.checkBudget(); }
        catch (error) { this.cancel(error.message); }
      }, 3000);
      this.monitor.unref();
    } catch (error) {
      this.close();
      throw error;
    }
  }

  /** Stops active child processes and prevents subsequent test targets from starting. */
  cancel(reason) { if (!this.controller.signal.aborted) this.controller.abort(new Error(reason)); }

  /** Enforces free-space and report budgets before and during execution. */
  checkBudget() {
    const disk = fs.statfsSync(this.runsRoot);
    if (disk.bavail * disk.bsize < 10 * 1024 ** 3) throw new Error('Less than 10 GiB free; stopping test run.');
    const directories = managedDirectories(this.runsRoot);
    let total = 0;
    for (const directory of directories) {
      const bytes = directoryBytes(directory);
      if (directory === this.runDir && bytes > 1024 ** 3) throw new Error('Active report exceeded its 1 GiB budget.');
      total += bytes;
    }
    if (total > 2 * 1024 ** 3) throw new Error('Managed reports exceeded their 2 GiB budget.');
  }

  /** Releases process handlers and the lock after all children have stopped. */
  close() {
    clearInterval(this.monitor);
    process.off('SIGINT', this.stopHandler);
    process.off('SIGTERM', this.stopHandler);
    if (this.locked) fs.rmSync(this.lockPath, { force: true });
    this.locked = false;
  }
}

/** Streams bounded log files and rotates complete segments without accumulating output in memory. */
export class RotatingLog {
  /** Starts a bounded log, rotating the previous invocation unless append is requested. */
  constructor(file, policy, append = false) {
    this.file = file;
    this.limit = policy.logMaxBytes;
    this.backups = policy.logBackups;
    fs.mkdirSync(path.dirname(file), { recursive: true });
    for (let index = 0; index <= 2; index += 1) {
      const previous = index ? `${file}.${index}` : file;
      if (index > this.backups) fs.rmSync(previous, { force: true });
      else if (fs.existsSync(previous) && fs.statSync(previous).size > this.limit) {
        const descriptor = fs.openSync(previous, 'r');
        const tail = Buffer.alloc(this.limit);
        try { fs.readSync(descriptor, tail, 0, tail.length, fs.fstatSync(descriptor).size - tail.length); }
        finally { fs.closeSync(descriptor); }
        fs.writeFileSync(previous, tail);
      }
    }
    this.size = fs.existsSync(file) ? fs.statSync(file).size : 0;
    if (!append && this.size) this.rotate();
    else this.descriptor = fs.openSync(file, append ? 'a' : 'w');
    if (!append) this.size = 0;
  }

  /** Retains at most the configured number of older log segments. */
  rotate() {
    this.close();
    fs.rmSync(`${this.file}.${this.backups}`, { force: true });
    for (let index = this.backups - 1; index >= 0; index -= 1) {
      const source = index ? `${this.file}.${index}` : this.file;
      if (fs.existsSync(source)) fs.renameSync(source, `${this.file}.${index + 1}`);
    }
    this.descriptor = fs.openSync(this.file, 'w');
    this.size = 0;
  }

  /** Splits even a single oversized chunk across bounded files. */
  write(text) {
    const bytes = Buffer.from(text);
    for (let offset = 0; offset < bytes.length;) {
      if (this.size >= this.limit) this.rotate();
      const count = Math.min(bytes.length - offset, this.limit - this.size);
      const written = fs.writeSync(this.descriptor, bytes, offset, count);
      if (written === 0) throw new Error(`Could not write log: ${this.file}`);
      offset += written;
      this.size += written;
    }
  }

  /** Closes the active log descriptor once. */
  close() {
    if (this.descriptor !== undefined) fs.closeSync(this.descriptor);
    this.descriptor = undefined;
  }
}

/** Runs one process with rotating logs, bounded diagnostic capture and process-group cancellation. */
export async function runLoggedCommand(command, args, options, output) {
  output.controller.signal.throwIfAborted();
  const startedAt = new Date();
  const log = new RotatingLog(options.logPath, output.policy);
  const redact = options.redact ?? (text => text);
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, {
      cwd: options.cwd, env: options.env ? { ...process.env, ...options.env } : process.env,
      shell: false, detached: process.platform !== 'win32', stdio: ['ignore', 'pipe', 'pipe']
    });
    let captured = '';
    let outputTruncated = false;
    let timedOut = false;
    let processError = null;
    let hardKill;
    let timeout;
    const stop = () => {
      const kill = signal => {
        try {
          if (process.platform !== 'win32' && child.pid) process.kill(-child.pid, signal);
          else child.kill(signal);
        } catch (error) { if (error.code !== 'ESRCH') processError = error; }
      };
      kill('SIGTERM');
      hardKill ??= setTimeout(() => kill('SIGKILL'), 3000);
    };
    const emit = (text, stream) => {
      try { log.write(text); }
      catch (error) { processError = error; stop(); }
      stream.write(text);
    };
    const stdoutRedaction = options.redact ? new RedactedText(redact, text => emit(text, process.stdout)) : null;
    const stderrRedaction = options.redact ? new RedactedText(redact, text => emit(text, process.stderr)) : null;
    const append = (chunk, stream) => {
      const text = chunk.toString();
      captured += text;
      if (Buffer.byteLength(captured) > 10 * MiB || outputTruncated) {
        captured = captured.slice(-64 * 1024);
        outputTruncated = true;
      }
      const redaction = stream === process.stdout ? stdoutRedaction : stderrRedaction;
      if (redaction) redaction.write(chunk);
      else emit(text, stream);
      if (captured.includes('Window was closed before render test case finished')) output.cancel('A test window was closed; remaining targets were cancelled.');
    };
    child.stdout.on('data', chunk => append(chunk, process.stdout));
    child.stderr.on('data', chunk => append(chunk, process.stderr));
    output.controller.signal.addEventListener('abort', stop, { once: true });
    if (options.timeoutMs > 0) timeout = setTimeout(() => {
      timedOut = true;
      append(`\n[timeout] ${command} exceeded ${options.timeoutMs} ms.\n`, process.stderr);
      stop();
    }, options.timeoutMs);
    child.on('error', error => { processError = error; });
    child.on('close', (code, signal) => {
      stdoutRedaction?.end();
      stderrRedaction?.end();
      if ((output.controller.signal.aborted || timedOut || processError) && child.pid && process.platform !== 'win32') {
        try { process.kill(-child.pid, 'SIGKILL'); }
        catch (error) { if (error.code !== 'ESRCH') processError = error; }
      }
      clearTimeout(timeout);
      clearTimeout(hardKill);
      output.controller.signal.removeEventListener('abort', stop);
      try { if (processError) log.write(redact(`${processError.message}\n`)); }
      finally { log.close(); }
      if (processError) { reject(processError); return; }
      const completedAt = new Date();
      resolve({ command, args, cwd: options.cwd, logPath: options.logPath,
        startedAt, completedAt, durationMs: completedAt - startedAt,
        exitCode: output.controller.signal.aborted ? 130 : timedOut ? 124 : (code ?? 1),
        signal: signal ?? '', timedOut, outputTruncated,
        output: outputTruncated ? `[output capture truncated; see rotated logs]\n${captured}` : captured
      });
    });
  });
}
