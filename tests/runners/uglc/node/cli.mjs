#!/usr/bin/env node

import { spawn } from 'node:child_process';
import { promises as fs } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

import {
  getConfigRelativePath,
  getDefaultProfileName,
  getExampleConfigRelativePath,
  getProfile,
  isMissingAuthoritativeConfigError
} from './test-config.mjs';
import { getTestRegistry, getTestSelection } from './test-registry.mjs';
import { validateUglirSelection } from './uglir-contract.mjs';
import { captureCompilerEvidence, verifyUnchangedCandidate } from '../../shared/compiler-evidence.mjs';

import { parseOutputPolicy, outputPolicyHelp, listReports, RunOutput, RotatingLog, runLoggedCommand } from '../../shared/output-policy.mjs';

let activeOutput = null;

const scriptPath = fileURLToPath(import.meta.url);
const scriptDir = path.dirname(scriptPath);
const sourceDir = path.resolve(scriptDir, '..', '..', '..', '..');

function escapeRegExp(value) {
  return String(value).replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

class PathRedactor {
  constructor(entries = []) {
    this.rules = [];
    for (const [rawPath, token] of entries) {
      this.add(rawPath, token);
    }
    this.rules.sort((left, right) => right.pattern.length - left.pattern.length);
  }

  static fromProfile(profile) {
    const roots = profile.redactionRoots ?? {};
    return new PathRedactor([
      [roots.sourceDir ?? sourceDir, '<UGLC>'],
      [roots.GVMRoot, '<GVM>'],
      [roots.IntegrationRoot, '<Integration>'],
      [roots.UGLHeadersDir, '<UGLHeaders>'],
      [roots.DependencyRoot, '<Deps>'],
      [roots.LLVMRoot, '<LLVM>'],
      [roots.homeDir ?? os.homedir(), '<Home>'],
      [os.tmpdir(), '<Temp>']
    ]);
  }

  add(rawPath, token) {
    if (typeof rawPath !== 'string' || rawPath.trim() === '') {
      return;
    }
    const normalizedPath = path.normalize(rawPath);
    const variants = new Set([
      rawPath,
      normalizedPath,
      normalizedPath.split(path.sep).join('/'),
      normalizedPath.split(path.sep).join('\\')
    ]);
    for (const variant of variants) {
      if (variant && variant !== path.parse(variant).root) {
        this.rules.push({
          pattern: variant,
          regex: new RegExp(escapeRegExp(variant), 'g'),
          token
        });
      }
    }
  }

  redact(value) {
    let result = String(value ?? '');
    for (const rule of this.rules) {
      result = result.replace(rule.regex, rule.token);
    }
    return result;
  }

  redactObject(value) {
    if (typeof value === 'string') {
      return this.redact(value);
    }
    if (Array.isArray(value)) {
      return value.map((entry) => this.redactObject(entry));
    }
    if (value && typeof value === 'object') {
      return Object.fromEntries(
        Object.entries(value).map(([key, entry]) => [key, this.redactObject(entry)])
      );
    }
    return value;
  }
}

let activeRedactor = PathRedactor.fromProfile({
  redactionRoots: {
    sourceDir,
    homeDir: os.homedir()
  }
});

function parseArgs(argv) {
  const [, , command, ...rest] = argv;
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
  return { command, options };
}

function parseBooleanOption(value, defaultValue) {
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

/** Converts command-line runner options into profile overrides used by every runner command. */
function createProfileOptions(options) {
  return {
    buildDirOverride: options['build-dir']
  };
}

function sanitizeSegment(value) {
  return String(value)
    .trim()
    .replaceAll(/[^a-zA-Z0-9._-]+/g, '-')
    .replaceAll(/-+/g, '-')
    .replaceAll(/^-|-$/g, '');
}

function escapeHtml(value) {
  return String(value ?? '')
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#39;');
}

function truncateText(value, maxChars = 8000) {
  const normalized = String(value ?? '');
  if (normalized.length <= maxChars) {
    return normalized;
  }
  return `${normalized.slice(0, maxChars)}\n...<truncated>`;
}

function formatCommand(command, args) {
  return [command, ...(args ?? [])].map((segment) => (
    /[\s"]/u.test(segment)
      ? `"${String(segment).replaceAll('"', '\\"')}"`
      : String(segment)
  )).join(' ');
}

function formatDuration(durationMs) {
  if (!Number.isFinite(durationMs)) {
    return '0 ms';
  }
  if (durationMs < 1000) {
    return `${Math.round(durationMs)} ms`;
  }
  if (durationMs < 60_000) {
    return `${(durationMs / 1000).toFixed(2)} s`;
  }
  return `${(durationMs / 60_000).toFixed(2)} min`;
}

function createUglcRuntimeEnv(profile) {
  if (!profile.clangResourceDir) {
    return undefined;
  }
  return {
    UGLC_LLVM_RESOURCE_DIR: profile.clangResourceDir
  };
}

function makeTimestamp(date) {
  const year = date.getFullYear();
  const month = String(date.getMonth() + 1).padStart(2, '0');
  const day = String(date.getDate()).padStart(2, '0');
  const hour = String(date.getHours()).padStart(2, '0');
  const minute = String(date.getMinutes()).padStart(2, '0');
  const second = String(date.getSeconds()).padStart(2, '0');
  const millisecond = String(date.getMilliseconds()).padStart(3, '0');
  return `${year}${month}${day}_${hour}${minute}${second}_${millisecond}`;
}

function buildRunId(startedAt, profileName, groupName, caseId = '') {
  return [
    makeTimestamp(startedAt),
    sanitizeSegment(profileName),
    sanitizeSegment(groupName),
    sanitizeSegment(caseId)
  ].filter(Boolean).join('__');
}

async function ensureDirectory(dirPath) {
  await fs.mkdir(dirPath, { recursive: true });
}

async function ensureCleanDirectory(dirPath) {
  await fs.rm(dirPath, { recursive: true, force: true });
  await ensureDirectory(dirPath);
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

async function loadJson(filePath) {
  return JSON.parse(await loadText(filePath));
}

async function writeJson(filePath, value) {
  await ensureDirectory(path.dirname(filePath));
  await fs.writeFile(filePath, JSON.stringify(value, null, 2));
}

async function listFilesRecursive(rootDir) {
  if (!(await pathExists(rootDir))) {
    return [];
  }
  const stack = [rootDir];
  const files = [];
  while (stack.length > 0) {
    const current = stack.pop();
    const entries = await fs.readdir(current, { withFileTypes: true });
    for (const entry of entries) {
      const entryPath = path.join(current, entry.name);
      if (entry.isDirectory()) {
        stack.push(entryPath);
      } else if (entry.isFile()) {
        files.push(entryPath);
      }
    }
  }
  return files.sort();
}

async function runCommand(command, args, options = {}) {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, {
      cwd: options.cwd ?? sourceDir,
      env: options.env ? { ...process.env, ...options.env } : process.env,
      shell: false,
      stdio: options.stdio ?? 'ignore'
    });
    child.on('error', reject);
    child.on('exit', (code) => resolve(code ?? 1));
  });
}

/** Executes a compiler check with bounded, redacted logs and complete diagnostic assertions. */
async function runStreamingCommand(command, args, options = {}) {
  const redactor = options.redactor ?? activeRedactor;
  const result = await runLoggedCommand(command, args, {
    ...options, cwd: options.cwd ?? sourceDir, redact: text => redactor.redact(text)
  }, activeOutput);
  // Assertions must not silently pass when an expected diagnostic was truncated.
  if (result.outputTruncated) {
    result.exitCode = 125;
    result.output = 'Compiler output exceeded the 10 MiB assertion capture limit.\n' + result.output;
  }
  return { ...result, startedAt: result.startedAt.toISOString(), completedAt: result.completedAt.toISOString() };
}

function toRelativeRunPath(filePath, runDir) {
  if (!filePath) {
    return '';
  }
  return path.relative(runDir, filePath) || '.';
}

async function tryOpenWithMacDefaultHandler(targetPath) {
  return (await runCommand('open', [targetPath])) === 0;
}

async function openReportArtifact(targetPath) {
  if (process.platform === 'darwin') {
    return tryOpenWithMacDefaultHandler(targetPath);
  }
  if (process.platform === 'win32') {
    return (await runCommand('cmd', ['/c', 'start', '', targetPath])) === 0;
  }
  return (await runCommand('xdg-open', [targetPath])) === 0;
}

function getPaths(profile) {
  return {
    runsRoot: profile.runsRoot
  };
}

/** Finds the newest complete report regardless of its directory naming mode. */
async function findMostRecentRunDir(paths) {
  const latest = listReports(paths.runsRoot)[0];
  return latest ? path.join(paths.runsRoot, latest.name) : null;
}

/** Lists retained reports by execution time while keeping their actual directory links. */
async function collectRecentRuns(paths, currentRunId) {
  return listReports(paths.runsRoot).filter(entry => entry.summary.run.runId !== currentRunId)
    .slice(0, 10).map(({ name, summary }) => ({
      runId: summary.run.runId, profile: summary.run.profile, group: summary.run.group,
      exitCode: summary.run.exitCode, totals: summary.totals,
      startedAt: summary.run.startedAt, generatedAt: summary.generatedAt,
      href: `../${name}/index.html`
    }));
}

async function verifyProfile(profile) {
  if (!profile.configAvailable) {
    throw new Error(profile.missingConfigMessage ?? `Missing ${getConfigRelativePath()}.`);
  }
  for (const entry of profile.requiredPathChecks ?? []) {
    if (!(await pathExists(entry.path))) {
      throw new Error(`${entry.fieldPath} does not exist.`);
    }
  }
}

async function runBuildStep(profile, runDir, redactor) {
  const [command, ...args] = profile.buildCommand;
  const buildLogPath = path.join(activeOutput.logDir, 'build.log');
  const result = await runStreamingCommand(command, args, {
    cwd: sourceDir,
    timeoutMs: profile.buildTimeoutMs,
    logPath: buildLogPath,
    redactor
  });
  return {
    id: 'build',
    title: 'Build UGLC',
    command: redactor.redact(command),
    args: redactor.redactObject(args),
    cwd: redactor.redact(result.cwd),
    exitCode: result.exitCode,
    signal: result.signal,
    startedAt: result.startedAt,
    completedAt: result.completedAt,
    durationMs: result.durationMs,
    timedOut: result.timedOut,
    commandDisplay: redactor.redact(formatCommand(command, args)),
    expectedExitCode: 0,
    logFile: toRelativeRunPath(buildLogPath, runDir),
    outputExcerpt: truncateText(redactor.redact(result.output), 12_000)
  };
}

function createFixturePlan(testCase, profile, caseDir) {
  const generatedDir = path.join(caseDir, 'generated');
  const hostCompileSteps = (testCase.hostCompileSteps ?? []).map((compileStep) => {
    const compileSourcePath = path.join(caseDir, `${compileStep.id}.cpp`);
    const generatedHeaderPath = path.join(generatedDir, 'generate_result.hpp');
    const generatedHeaderInclude = path.relative(caseDir, generatedHeaderPath).split(path.sep).join('/');
    const includeArgs = [...(profile.hostIncludeDirs ?? []), ...(compileStep.includeDirs ?? [])]
      .flatMap((includeDir) => ['-I', includeDir]);

    return {
      id: compileStep.id,
      title: compileStep.title,
      command: profile.hostCompiler,
      args: [
        ...(profile.hostCompileBaseArgs ?? ['-std=c++20', '-fsyntax-only']),
        ...(compileStep.compilerArgs ?? []),
        ...includeArgs,
        compileSourcePath
      ],
      cwd: sourceDir,
      expectedExitCode: compileStep.expectedExitCode ?? 0,
      timeoutMs: compileStep.timeoutMs ?? testCase.timeoutMs,
      prepare: async () => {
        // These post-generation host compile probes let us validate that a
        // generated header is safe to include, while still rejecting actual
        // host-side calls to deleted shader-only entry points.
        await fs.writeFile(
          compileSourcePath,
          compileStep.sourceText.replaceAll('__GENERATED_HEADER__', generatedHeaderInclude)
        );
      }
    };
  });

  return {
    artifactRoot: generatedDir,
    previewFiles: ['generate_result.hpp', 'exports.hpp', 'dsl_single_header.hpp'],
    steps: [
      {
        id: 'uglc',
        title: 'Run UGLC on DSL fixture',
        command: profile.uglcExecutable,
        args: [
          '-s', testCase.absoluteSourcePath,
          '--dsl-dir', path.dirname(testCase.absoluteSourcePath),
          ...(profile.clangResourceDir ? ['--resource-dir', profile.clangResourceDir] : []),
          '-I', path.dirname(testCase.absoluteSourcePath),
          ...(profile.uglcIncludeDirs ?? []).flatMap((includeDir) => ['-I', includeDir]),
          ...(testCase.extraIncludeDirs ?? []).flatMap((includeDir) => ['-I', includeDir]),
          '-I', testCase.uglHeadersDir,
          ...(testCase.uglcArgs ?? []),
          '-o', generatedDir
        ],
        cwd: sourceDir,
        env: { ...createUglcRuntimeEnv(profile), ...(testCase.uglcEnv ?? {}) },
        expectedExitCode: testCase.expectedExitCode,
        timeoutMs: testCase.timeoutMs,
        sanitizeArtifacts: true,
        prepare: async () => {
          await ensureDirectory(generatedDir);
          for (const [relativePath, content] of Object.entries(testCase.initialArtifacts ?? {})) {
            const artifactPath = path.join(generatedDir, relativePath);
            await ensureDirectory(path.dirname(artifactPath));
            await fs.writeFile(artifactPath, content, 'utf8');
          }
        }
      },
      ...hostCompileSteps
    ]
  };
}

function createShadowFixturePlan(testCase, profile, caseDir) {
  const generatedDir = path.join(caseDir, 'generated');
  const legacyGeneratedDir = path.join(generatedDir, 'legacy');
  const experimentalGeneratedDir = path.join(generatedDir, 'uglir');
  const commonArgs = [
    '-s', testCase.absoluteSourcePath,
    '--dsl-dir', path.dirname(testCase.absoluteSourcePath),
    ...(profile.clangResourceDir ? ['--resource-dir', profile.clangResourceDir] : []),
    '-I', path.dirname(testCase.absoluteSourcePath),
    ...(profile.uglcIncludeDirs ?? []).flatMap((includeDir) => ['-I', includeDir]),
    ...(testCase.extraIncludeDirs ?? []).flatMap((includeDir) => ['-I', includeDir]),
    '-I', testCase.uglHeadersDir
  ];

  return {
    artifactRoot: generatedDir,
    previewFiles: ['legacy/generate_result.hpp', 'uglir/generate_result.hpp'],
    steps: [
      {
        id: 'uglc-legacy',
        title: 'Run legacy UGLC on DSL fixture',
        command: profile.uglcExecutable,
        args: [
          ...commonArgs,
          '--shader-pipeline=legacy',
          '-o', legacyGeneratedDir
        ],
        cwd: sourceDir,
        env: createUglcRuntimeEnv(profile),
        expectedExitCode: 0,
        timeoutMs: testCase.timeoutMs,
        sanitizeArtifacts: true,
        prepare: async () => {
          await ensureDirectory(legacyGeneratedDir);
        }
      },
      {
        id: 'uglc-experimental',
        title: 'Run UGLIR UGLC on DSL fixture',
        command: profile.uglcExecutable,
        args: [
          ...commonArgs,
          '--shader-pipeline=uglir',
          '-o', experimentalGeneratedDir
        ],
        cwd: sourceDir,
        env: createUglcRuntimeEnv(profile),
        expectedExitCode: 0,
        timeoutMs: testCase.timeoutMs,
        sanitizeArtifacts: true,
        prepare: async () => {
          await ensureDirectory(experimentalGeneratedDir);
        }
      }
    ]
  };
}

function createHarnessPlan(testCase, profile, caseDir) {
  const binDir = path.join(caseDir, 'bin');
  const executablePath = path.join(binDir, testCase.id);
  const includeArgs = [
    path.join(sourceDir, 'UGLC', 'Source'),
    path.join(sourceDir, 'UGLC', 'Source', 'CodeGen'),
    ...(profile.hostIncludeDirs ?? [])
  ].flatMap((includeDir) => ['-I', includeDir]);
  const runArgs = typeof testCase.runArgs === 'function'
    ? testCase.runArgs({ profile, testCase, caseDir, executablePath })
    : (testCase.runArgs ?? []);
  return {
    artifactRoot: binDir,
    previewFiles: [],
    steps: [
      {
        id: 'compile',
        title: 'Compile white-box harness',
        command: profile.hostCompiler,
        args: [
          '-std=c++20',
          '-DLLVM_DISABLE_ABI_BREAKING_CHECKS_ENFORCING=1',
          ...includeArgs,
          testCase.absoluteSourcePath,
          ...(testCase.extraSourcesAbsolutePaths ?? []),
          '-o',
          executablePath
        ],
        cwd: sourceDir,
        expectedExitCode: 0,
        timeoutMs: testCase.timeoutMs,
        prepare: async () => {
          await ensureDirectory(binDir);
        }
      },
      {
        id: 'run',
        title: 'Run white-box harness',
        command: executablePath,
        args: runArgs,
        cwd: sourceDir,
        env: createUglcRuntimeEnv(profile),
        expectedExitCode: 0,
        timeoutMs: testCase.timeoutMs
      }
    ]
  };
}

function createCasePlan(testCase, profile, caseDir) {
  if (testCase.kind === 'uglc-fixture') {
    return createFixturePlan(testCase, profile, caseDir);
  }
  if (testCase.kind === 'uglc-shadow-fixture') {
    return createShadowFixturePlan(testCase, profile, caseDir);
  }
  if (testCase.kind === 'host-harness') {
    return createHarnessPlan(testCase, profile, caseDir);
  }
  throw new Error(`Unsupported test kind: ${testCase.kind}`);
}

function createAssertionContext(testCase, plan, runDir, stepRecords, redactor) {
  const checks = [];

  function recordCheck(description, passed, details = '') {
    checks.push({
      description,
      passed,
      details: truncateText(redactor.redact(details), 4000)
    });
  }

  function getStep(stepId) {
    return stepRecords.find((entry) => entry.id === stepId) ?? null;
  }

  async function readArtifact(relativePath) {
    if (!plan.artifactRoot) {
      throw new Error(`Case "${testCase.id}" does not define an artifact root.`);
    }
    return loadText(path.join(plan.artifactRoot, relativePath));
  }

  return {
    checks,
    recordCheck,
    getStep,
    readArtifact,
    async expectArtifactExists(relativePath, description) {
      const absolutePath = path.join(plan.artifactRoot, relativePath);
      const exists = await pathExists(absolutePath);
      recordCheck(description, exists, exists ? `Found ${relativePath}` : `Missing ${relativePath}`);
      return exists;
    },
    /** Records that an expected generated artifact is absent from the case artifact root. */
    async expectArtifactNotExists(relativePath, description) {
      const absolutePath = path.join(plan.artifactRoot, relativePath);
      const exists = await pathExists(absolutePath);
      recordCheck(description, !exists, exists ? `Unexpected artifact exists: ${relativePath}` : `Confirmed missing artifact: ${relativePath}`);
      return !exists;
    },
    async expectFileContains(relativePath, needle, description) {
      const absolutePath = path.join(plan.artifactRoot, relativePath);
      if (!(await pathExists(absolutePath))) {
        recordCheck(description, false, `Missing artifact: ${relativePath}`);
        return false;
      }
      const content = await loadText(absolutePath);
      const passed = content.includes(needle);
      recordCheck(description, passed, passed ? `Found expected text in ${relativePath}` : `Could not find expected text in ${relativePath}: ${needle}`);
      return passed;
    },
    async expectFileNotContains(relativePath, needle, description) {
      const absolutePath = path.join(plan.artifactRoot, relativePath);
      if (!(await pathExists(absolutePath))) {
        recordCheck(description, false, `Missing artifact: ${relativePath}`);
        return false;
      }
      const content = await loadText(absolutePath);
      const passed = !content.includes(needle);
      recordCheck(description, passed, passed ? `Confirmed text is absent from ${relativePath}` : `Unexpected text found in ${relativePath}: ${needle}`);
      return passed;
    },
    async expectFileContainsInOrder(relativePath, needles, description) {
      const absolutePath = path.join(plan.artifactRoot, relativePath);
      if (!(await pathExists(absolutePath))) {
        recordCheck(description, false, `Missing artifact: ${relativePath}`);
        return false;
      }
      const content = await loadText(absolutePath);
      let searchStart = 0;
      for (const needle of needles) {
        const foundAt = content.indexOf(needle, searchStart);
        if (foundAt < 0) {
          recordCheck(description, false, `Could not find expected ordered text after offset ${searchStart} in ${relativePath}: ${needle}`);
          return false;
        }
        searchStart = foundAt + needle.length;
      }
      recordCheck(description, true, `Found ${needles.length} ordered text fragments in ${relativePath}`);
      return true;
    },
    async expectFileOccurrenceCount(relativePath, needle, expectedCount, description) {
      const absolutePath = path.join(plan.artifactRoot, relativePath);
      if (!(await pathExists(absolutePath))) {
        recordCheck(description, false, `Missing artifact: ${relativePath}`);
        return false;
      }
      const content = await loadText(absolutePath);
      let count = 0;
      let searchStart = 0;
      while (true) {
        const foundAt = content.indexOf(needle, searchStart);
        if (foundAt < 0) {
          break;
        }
        ++count;
        searchStart = foundAt + needle.length;
      }
      const passed = count === expectedCount;
      recordCheck(description, passed, passed ? `Found ${expectedCount} occurrences in ${relativePath}` : `Expected ${expectedCount} occurrences in ${relativePath}, found ${count}: ${needle}`);
      return passed;
    },
    async expectStepOutputContains(stepId, needle, description) {
      const step = getStep(stepId);
      if (!step) {
        recordCheck(description, false, `Step not found: ${stepId}`);
        return false;
      }
      const passed = step.output.includes(needle);
      recordCheck(description, passed, passed ? `Found expected text in step ${stepId}` : `Could not find expected text in step ${stepId}: ${needle}`);
      return passed;
    },
    async expectStepOutputNotContains(stepId, needle, description) {
      const step = getStep(stepId);
      if (!step) {
        recordCheck(description, false, `Step not found: ${stepId}`);
        return false;
      }
      const passed = !step.output.includes(needle);
      recordCheck(description, passed, passed ? `Confirmed text is absent from step ${stepId}` : `Found unexpected text in step ${stepId}: ${needle}`);
      return passed;
    }
  };
}

async function collectArtifactPreviews(plan, runDir, redactor) {
  const previews = [];
  if (!plan.artifactRoot) {
    return previews;
  }
  for (const fileName of plan.previewFiles ?? []) {
    const absolutePath = path.join(plan.artifactRoot, fileName);
    if (!(await pathExists(absolutePath))) {
      continue;
    }
    previews.push({
      fileName,
      relativePath: toRelativeRunPath(absolutePath, runDir),
      content: truncateText(redactor.redact(await loadText(absolutePath)), 8000)
    });
  }
  return previews;
}

async function redactTextArtifacts(rootDir, redactor) {
  for (const filePath of await listFilesRecursive(rootDir)) {
    const content = await loadText(filePath);
    const redactedContent = redactor.redact(content);
    if (redactedContent !== content) {
      await fs.writeFile(filePath, redactedContent);
    }
  }
}

function redactStepRecordForReport(stepRecord, redactor) {
  return {
    id: stepRecord.id,
    title: stepRecord.title,
    command: redactor.redact(stepRecord.command),
    args: redactor.redactObject(stepRecord.args),
    commandDisplay: redactor.redact(stepRecord.commandDisplay),
    cwd: redactor.redact(stepRecord.cwd),
    expectedExitCode: stepRecord.expectedExitCode,
    exitCode: stepRecord.exitCode,
    signal: stepRecord.signal,
    startedAt: stepRecord.startedAt,
    completedAt: stepRecord.completedAt,
    durationMs: stepRecord.durationMs,
    timedOut: stepRecord.timedOut,
    outputExcerpt: truncateText(redactor.redact(stepRecord.output), 12_000),
    logFile: stepRecord.logFile
  };
}

async function runSingleCase(testCase, profile, runDir, redactor) {
  const caseDir = path.join(runDir, 'cases', testCase.id);
  await ensureCleanDirectory(caseDir);

  const plan = createCasePlan(testCase, profile, caseDir);
  const stepRecords = [];
  const logsDir = path.join(activeOutput.logDir, testCase.id);

  for (const step of plan.steps) {
    if (step.prepare) {
      await step.prepare();
    }
    const logPath = path.join(logsDir, `${step.id}.log`);
    let result;
    try {
      result = await runStreamingCommand(step.command, step.args, {
        cwd: step.cwd ?? sourceDir,
        env: step.env,
        timeoutMs: step.timeoutMs ?? profile.defaultTimeoutMs,
        logPath,
        redactor
      });
    } catch (error) {
      const now = new Date().toISOString();
      result = {
        command: step.command,
        args: step.args,
        cwd: step.cwd ?? sourceDir,
        exitCode: 127,
        signal: '',
        startedAt: now,
        completedAt: now,
        durationMs: 0,
        timedOut: false,
        output: error instanceof Error ? error.message : String(error),
        logPath
      };
      await ensureDirectory(path.dirname(logPath));
      await fs.writeFile(logPath, redactor.redact(result.output));
    }
    stepRecords.push({
      id: step.id,
      title: step.title,
      command: step.command,
      args: step.args,
      commandDisplay: formatCommand(step.command, step.args),
      cwd: step.cwd ?? sourceDir,
      expectedExitCode: step.expectedExitCode ?? 0,
      exitCode: result.exitCode,
      signal: result.signal,
      startedAt: result.startedAt,
      completedAt: result.completedAt,
      durationMs: result.durationMs,
      timedOut: result.timedOut,
      output: result.output,
      outputExcerpt: truncateText(result.output, 12_000),
      logFile: toRelativeRunPath(logPath, runDir)
    });
    if (activeOutput.controller.signal.aborted) break;
    if (step.sanitizeArtifacts && plan.artifactRoot) {
      await redactTextArtifacts(plan.artifactRoot, redactor);
    }
    if (result.exitCode !== (step.expectedExitCode ?? 0)) {
      break;
    }
  }

  const assertionContext = createAssertionContext(testCase, plan, runDir, stepRecords, redactor);
  for (const stepRecord of stepRecords) {
    const passed = stepRecord.exitCode === stepRecord.expectedExitCode;
    assertionContext.recordCheck(
      `${stepRecord.title} exit code should be ${stepRecord.expectedExitCode}`,
      passed,
      passed
        ? `Observed expected exit code ${stepRecord.exitCode}`
        : `Expected ${stepRecord.expectedExitCode}, observed ${stepRecord.exitCode}`
    );
  }

  try {
    if (typeof testCase.verify === 'function') {
      await testCase.verify(assertionContext);
    }
  } catch (error) {
    assertionContext.recordCheck(
      'Case verification should complete without throwing',
      false,
      error instanceof Error ? `${error.message}\n${error.stack ?? ''}` : String(error)
    );
  }

  const artifactFiles = await listFilesRecursive(plan.artifactRoot);
  const artifactPreviews = await collectArtifactPreviews(plan, runDir, redactor);
  const failedChecks = assertionContext.checks.filter((entry) => !entry.passed);
  const durationMs = stepRecords.reduce((sum, entry) => sum + entry.durationMs, 0);
  const reportStepRecords = stepRecords.map((entry) => redactStepRecordForReport(entry, redactor));

  return {
    id: testCase.id,
    title: testCase.title,
    group: testCase.group,
    labels: testCase.labels,
    kind: testCase.kind,
    description: testCase.description,
    validates: testCase.validates,
    watchouts: testCase.watchouts,
    status: failedChecks.length === 0 ? 'pass' : 'fail',
    durationMs,
    sourceFile: path.relative(sourceDir, testCase.absoluteSourcePath),
    steps: reportStepRecords,
    checks: assertionContext.checks,
    artifacts: artifactFiles.map((filePath) => toRelativeRunPath(filePath, runDir)),
    artifactPreviews
  };
}

function buildSkippedCaseRecord(testCase, reason, redactor = activeRedactor) {
  return {
    id: testCase.id,
    title: testCase.title,
    group: testCase.group,
    labels: testCase.labels,
    kind: testCase.kind,
    description: testCase.description,
    validates: testCase.validates,
    watchouts: testCase.watchouts,
    status: 'skip',
    durationMs: 0,
    sourceFile: path.relative(sourceDir, testCase.absoluteSourcePath),
    steps: [],
    checks: [
      {
        description: 'Case execution was skipped',
        passed: false,
        details: redactor.redact(reason)
      }
    ],
    artifacts: [],
    artifactPreviews: []
  };
}

function summarizeCases(caseRecords) {
  return caseRecords.reduce((accumulator, record) => {
    accumulator.total += 1;
    if (record.status === 'pass') {
      accumulator.pass += 1;
    } else if (record.status === 'fail') {
      accumulator.fail += 1;
    } else {
      accumulator.skip += 1;
    }
    return accumulator;
  }, { total: 0, pass: 0, fail: 0, skip: 0 });
}

function renderChecks(checks) {
  return checks.map((entry) => `
    <tr class="${entry.passed ? 'status-pass' : 'status-fail'}">
      <td>${entry.passed ? 'PASS' : 'FAIL'}</td>
      <td>${escapeHtml(entry.description)}</td>
      <td><pre>${escapeHtml(entry.details)}</pre></td>
    </tr>
  `).join('');
}

function renderSteps(steps) {
  if (steps.length === 0) {
    return '<p class="empty-text">No command steps were executed for this case.</p>';
  }
  return steps.map((step) => `
    <details class="step-card">
      <summary>
        <span class="step-title">${escapeHtml(step.title)}</span>
        <span class="pill ${step.exitCode === step.expectedExitCode ? 'pill-pass' : 'pill-fail'}">exit ${step.exitCode}</span>
        <span class="step-duration">${escapeHtml(formatDuration(step.durationMs))}</span>
      </summary>
      <div class="step-body">
        <dl class="meta-grid">
          <div><dt>Command</dt><dd>${escapeHtml(step.commandDisplay)}</dd></div>
          <div><dt>Expected Exit</dt><dd>${escapeHtml(step.expectedExitCode)}</dd></div>
          <div><dt>Observed Exit</dt><dd>${escapeHtml(step.exitCode)}</dd></div>
          <div><dt>Duration</dt><dd>${escapeHtml(formatDuration(step.durationMs))}</dd></div>
          <div><dt>Started</dt><dd>${escapeHtml(step.startedAt)}</dd></div>
          <div><dt>Log File</dt><dd>${escapeHtml(step.logFile)}</dd></div>
        </dl>
        <pre>${escapeHtml(step.outputExcerpt)}</pre>
      </div>
    </details>
  `).join('');
}

function renderArtifacts(caseRecord) {
  if (caseRecord.artifactPreviews.length === 0) {
    return '<p class="empty-text">No inline artifact previews were captured for this case.</p>';
  }
  return caseRecord.artifactPreviews.map((preview) => `
    <details class="artifact-card">
      <summary>${escapeHtml(preview.fileName)} <span class="artifact-path">${escapeHtml(preview.relativePath)}</span></summary>
      <pre>${escapeHtml(preview.content)}</pre>
    </details>
  `).join('');
}

function renderCaseCards(caseRecords) {
  return caseRecords.map((caseRecord) => `
    <details class="case-card ${caseRecord.status}">
      <summary class="case-summary">
        <div class="case-summary-main">
          <h3>${escapeHtml(caseRecord.title)}</h3>
          <p class="case-summary-description">${escapeHtml(caseRecord.description)}</p>
        </div>
        <div class="case-summary-side">
          <div class="case-meta">
            <span class="pill pill-neutral">${escapeHtml(caseRecord.group)}</span>
            <span class="pill ${caseRecord.status === 'pass' ? 'pill-pass' : (caseRecord.status === 'fail' ? 'pill-fail' : 'pill-skip')}">${escapeHtml(caseRecord.status.toUpperCase())}</span>
            <span class="pill pill-neutral">${escapeHtml(formatDuration(caseRecord.durationMs))}</span>
          </div>
          <code>${escapeHtml(caseRecord.id)}</code>
        </div>
      </summary>
      <div class="case-body">
        <dl class="meta-grid">
          <div><dt>Source</dt><dd>${escapeHtml(caseRecord.sourceFile)}</dd></div>
          <div><dt>Kind</dt><dd>${escapeHtml(caseRecord.kind)}</dd></div>
          <div><dt>Labels</dt><dd>${escapeHtml(caseRecord.labels.join(', '))}</dd></div>
        </dl>
        <div class="list-block">
          <h4>Validates</h4>
          <ul>${caseRecord.validates.map((entry) => `<li>${escapeHtml(entry)}</li>`).join('')}</ul>
        </div>
        <div class="list-block">
          <h4>Watchouts</h4>
          <ul>${caseRecord.watchouts.map((entry) => `<li>${escapeHtml(entry)}</li>`).join('')}</ul>
        </div>
        <div class="table-wrap">
          <table>
            <thead>
              <tr><th>Status</th><th>Check</th><th>Details</th></tr>
            </thead>
            <tbody>${renderChecks(caseRecord.checks)}</tbody>
          </table>
        </div>
        <div class="subsection">
          <h4>Command Steps</h4>
          ${renderSteps(caseRecord.steps)}
        </div>
        <div class="subsection">
          <h4>Artifact Preview</h4>
          ${renderArtifacts(caseRecord)}
        </div>
      </div>
    </details>
  `).join('');
}

function renderHistory(history) {
  if (history.length === 0) {
    return '<p class="empty-text">No earlier timestamped runs were found yet.</p>';
  }
  return `
    <table>
      <thead>
        <tr><th>Run</th><th>Profile</th><th>Group</th><th>Exit</th><th>Totals</th><th>Started</th><th>Report</th></tr>
      </thead>
      <tbody>
        ${history.map((entry) => `
          <tr>
            <td>${escapeHtml(entry.runId)}</td>
            <td>${escapeHtml(entry.profile)}</td>
            <td>${escapeHtml(entry.group)}</td>
            <td>${escapeHtml(entry.exitCode)}</td>
            <td>${escapeHtml(`${entry.totals.pass} pass / ${entry.totals.fail} fail / ${entry.totals.skip} skip`)}</td>
            <td>${escapeHtml(entry.startedAt)}</td>
            <td><a href="${escapeHtml(entry.href)}">Open report</a></td>
          </tr>
        `).join('')}
      </tbody>
    </table>
  `;
}

function renderHtml(summary) {
  const buildPassed = summary.build.exitCode === 0;
  return `<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>UGLC Test Report - ${escapeHtml(summary.run.runId)}</title>
  <style>
    :root {
      color-scheme: light;
      --bg: #f6f5f1;
      --panel: #ffffff;
      --border: #d6d1c5;
      --text: #1f2328;
      --muted: #5f6b7a;
      --pass: #146c2e;
      --pass-bg: #dff5e4;
      --fail: #9f1d1d;
      --fail-bg: #fde2e2;
      --skip: #8a6500;
      --skip-bg: #fff3d1;
      --neutral-bg: #ece7dc;
      --shadow: 0 10px 24px rgba(60, 52, 40, 0.08);
      --mono: "SFMono-Regular", "Consolas", "Menlo", monospace;
      --sans: "Avenir Next", "Segoe UI", sans-serif;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      background: linear-gradient(180deg, #f0eadf 0%, var(--bg) 280px);
      color: var(--text);
      font-family: var(--sans);
      line-height: 1.5;
    }
    main {
      max-width: 1360px;
      margin: 0 auto;
      padding: 32px 24px 64px;
    }
    h1, h2, h3, h4 { margin: 0 0 12px; line-height: 1.2; }
    p { margin: 0 0 12px; }
    a { color: inherit; }
    pre {
      margin: 0;
      white-space: pre-wrap;
      word-break: break-word;
      font-family: var(--mono);
      font-size: 12px;
      background: #151718;
      color: #f6f8fa;
      border-radius: 12px;
      padding: 16px;
      overflow: auto;
    }
    code { font-family: var(--mono); }
    table {
      width: 100%;
      border-collapse: collapse;
      background: var(--panel);
    }
    th, td {
      padding: 12px;
      border-bottom: 1px solid #ebe6dc;
      vertical-align: top;
      text-align: left;
    }
    th { background: #f8f4ec; }
    .hero, .panel, .case-card {
      background: var(--panel);
      border: 1px solid var(--border);
      border-radius: 20px;
      box-shadow: var(--shadow);
    }
    .hero {
      padding: 28px;
      margin-bottom: 24px;
    }
    .hero-grid, .summary-grid, .meta-grid {
      display: grid;
      gap: 16px;
    }
    .hero-grid { grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); margin-top: 20px; }
    .summary-grid { grid-template-columns: repeat(auto-fit, minmax(160px, 1fr)); margin: 24px 0; }
    .meta-grid { grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); }
    .meta-grid dt {
      font-size: 12px;
      color: var(--muted);
      text-transform: uppercase;
      letter-spacing: 0.08em;
      margin-bottom: 6px;
    }
    .meta-grid dd {
      margin: 0;
      font-weight: 600;
    }
    .panel {
      padding: 24px;
      margin-bottom: 24px;
    }
    .case-card {
      padding: 24px;
      margin-bottom: 20px;
      border-left-width: 8px;
    }
    .case-card.pass { border-left-color: var(--pass); }
    .case-card.fail { border-left-color: var(--fail); }
    .case-card.skip { border-left-color: var(--skip); }
    .case-summary {
      display: flex;
      justify-content: space-between;
      gap: 16px;
      align-items: start;
      cursor: pointer;
      list-style: none;
    }
    .case-summary::-webkit-details-marker { display: none; }
    .case-summary-main { flex: 1 1 auto; min-width: 0; }
    .case-summary-side {
      display: flex;
      flex-direction: column;
      align-items: end;
      gap: 10px;
      flex: 0 0 auto;
    }
    .case-summary-description {
      color: var(--muted);
      margin-top: 6px;
      margin-bottom: 0;
    }
    .case-body { margin-top: 18px; }
    .case-meta {
      display: flex;
      flex-wrap: wrap;
      gap: 8px;
    }
    .pill {
      display: inline-flex;
      align-items: center;
      border-radius: 999px;
      padding: 4px 10px;
      font-size: 12px;
      font-weight: 700;
      letter-spacing: 0.02em;
    }
    .pill-pass { background: var(--pass-bg); color: var(--pass); }
    .pill-fail { background: var(--fail-bg); color: var(--fail); }
    .pill-skip { background: var(--skip-bg); color: var(--skip); }
    .pill-neutral { background: var(--neutral-bg); color: #4e4a43; }
    .summary-card {
      border-radius: 18px;
      padding: 18px;
      border: 1px solid var(--border);
      background: #faf7f0;
    }
    .summary-card strong {
      display: block;
      font-size: 28px;
      margin-top: 8px;
    }
    .table-wrap {
      overflow: auto;
      border: 1px solid #ebe6dc;
      border-radius: 16px;
      margin: 18px 0;
    }
    .status-pass td:first-child { color: var(--pass); font-weight: 700; }
    .status-fail td:first-child { color: var(--fail); font-weight: 700; }
    .step-card, .artifact-card {
      border: 1px solid #ebe6dc;
      border-radius: 16px;
      background: #fbfaf7;
      margin-bottom: 12px;
      overflow: hidden;
    }
    .step-card summary, .artifact-card summary {
      cursor: pointer;
      list-style: none;
      padding: 14px 16px;
      display: flex;
      flex-wrap: wrap;
      gap: 10px;
      align-items: center;
      background: #f8f4ec;
    }
    .step-card summary::-webkit-details-marker,
    .artifact-card summary::-webkit-details-marker { display: none; }
    .step-body { padding: 16px; }
    .step-title { font-weight: 700; }
    .step-duration, .artifact-path { color: var(--muted); font-size: 12px; }
    .empty-text { color: var(--muted); }
    .subsection { margin-top: 20px; }
    .list-block ul { margin: 0; padding-left: 20px; }
    @media (max-width: 720px) {
      main { padding: 20px 16px 48px; }
      .hero, .panel, .case-card { padding: 20px; }
      .case-summary { flex-direction: column; }
      .case-summary-side { align-items: start; }
    }
  </style>
</head>
<body>
  <main>
    <section class="hero">
      <h1>UGLC Test Report</h1>
      <p>Node-driven regression report for the C++ DSL compiler. This HTML file is fully static and self-contained.</p>
      <div class="hero-grid">
        <div><strong>Run ID</strong><div>${escapeHtml(summary.run.runId)}</div></div>
        <div><strong>Profile</strong><div>${escapeHtml(summary.run.profile)}</div></div>
        <div><strong>Group</strong><div>${escapeHtml(summary.run.group)}</div></div>
        <div><strong>Exit Code</strong><div>${escapeHtml(summary.run.exitCode)}</div></div>
        <div><strong>Started</strong><div>${escapeHtml(summary.run.startedAt)}</div></div>
        <div><strong>Completed</strong><div>${escapeHtml(summary.run.completedAt)}</div></div>
      </div>
      <div class="summary-grid">
        <div class="summary-card"><span>Total Cases</span><strong>${escapeHtml(summary.totals.total)}</strong></div>
        <div class="summary-card"><span>Passed</span><strong>${escapeHtml(summary.totals.pass)}</strong></div>
        <div class="summary-card"><span>Failed</span><strong>${escapeHtml(summary.totals.fail)}</strong></div>
        <div class="summary-card"><span>Skipped</span><strong>${escapeHtml(summary.totals.skip)}</strong></div>
      </div>
    </section>

    <section class="panel">
      <h2>Build Step</h2>
      <dl class="meta-grid">
        <div><dt>Status</dt><dd><span class="pill ${buildPassed ? 'pill-pass' : 'pill-fail'}">${buildPassed ? 'PASS' : 'FAIL'}</span></dd></div>
        <div><dt>Duration</dt><dd>${escapeHtml(formatDuration(summary.build.durationMs))}</dd></div>
        <div><dt>Command</dt><dd><code>${escapeHtml(summary.build.commandDisplay)}</code></dd></div>
        <div><dt>Log File</dt><dd>${escapeHtml(summary.build.logFile)}</dd></div>
      </dl>
      <details class="step-card" open>
        <summary>
          <span class="step-title">Build Output</span>
          <span class="pill ${buildPassed ? 'pill-pass' : 'pill-fail'}">${buildPassed ? 'PASS' : 'FAIL'}</span>
        </summary>
        <div class="step-body">
          <pre>${escapeHtml(summary.build.outputExcerpt)}</pre>
        </div>
      </details>
    </section>

    <section class="panel">
      <h2>Recent Runs</h2>
      ${renderHistory(summary.history)}
    </section>

    <section class="panel">
      <h2>Case Results</h2>
      ${renderCaseCards(summary.cases)}
    </section>
  </main>
</body>
</html>`;
}

async function writeRunArtifacts(runDir, summary, redactor) {
  const redactedSummary = redactor.redactObject(summary);
  await writeJson(path.join(runDir, 'summary.json'), redactedSummary);
  await fs.writeFile(path.join(runDir, 'index.html'), renderHtml(redactedSummary));
}

async function runSuite(options) {
  const outputPolicy = parseOutputPolicy(options);
  const profileName = options.profile ?? getDefaultProfileName(sourceDir);
  const profile = getProfile(sourceDir, profileName, createProfileOptions(options));
  const redactor = PathRedactor.fromProfile(profile);
  activeRedactor = redactor;
  const groupName = options.group ?? 'all';
  const caseId = options.case ?? '';
  const shouldBuild = !parseBooleanOption(options['no-build'], false);
  const shouldOpenReport = parseBooleanOption(options['open-report'], false)
    && !parseBooleanOption(options['no-open-report'], false);
  const registry = getTestRegistry(sourceDir, profile);
  const selectedCases = getTestSelection(registry, groupName, caseId);
  validateUglirSelection(profile, registry, selectedCases, !caseId && (groupName === 'all' || groupName === 'uglir'));

  if (selectedCases.length === 0) {
    throw new Error(`No test cases matched selection: group="${groupName}", case="${caseId}"`);
  }

  await verifyProfile(profile);

  const runStartedAt = new Date();
  const runId = buildRunId(runStartedAt, profile.name, groupName, caseId);
  const paths = getPaths(profile);
  activeOutput = new RunOutput(paths.runsRoot, runId, outputPolicy);
  const runDir = activeOutput.runDir;

  let buildRecord;
  if (shouldBuild) {
    buildRecord = await runBuildStep(profile, runDir, redactor);
  } else {
    buildRecord = {
      id: 'build',
      title: 'Build UGLC',
      commandDisplay: '<skipped>',
      exitCode: 0,
      expectedExitCode: 0,
      durationMs: 0,
      logFile: '',
      outputExcerpt: 'Build step skipped by --no-build.'
    };
  }

  const caseRecords = [];
  let evidence = null;
  let evidenceError = '';
  if (buildRecord.exitCode === 0) {
    evidence = await captureCompilerEvidence(sourceDir, profile.buildDir);
    for (const testCase of selectedCases) {
      if (activeOutput.controller.signal.aborted) break;
      caseRecords.push(testCase.skipReason ? buildSkippedCaseRecord(testCase, testCase.skipReason, redactor) : await runSingleCase(testCase, profile, runDir, redactor));
    }
    try { verifyUnchangedCandidate(evidence, await captureCompilerEvidence(sourceDir, profile.buildDir)); }
    catch (error) { evidenceError = error.message; }
  } else {
    for (const testCase of selectedCases) {
      caseRecords.push(buildSkippedCaseRecord(testCase, 'The build step failed before case execution started.', redactor));
    }
  }

  const runCompletedAt = new Date();
  if (activeOutput.controller.signal.aborted) evidenceError = activeOutput.controller.signal.reason.message;
  const totals = summarizeCases(caseRecords);
  const exitCode = buildRecord.exitCode === 0 && totals.fail === 0 && totals.pass + totals.fail > 0 && !evidenceError ? 0 : 1;
  const history = await collectRecentRuns(paths, runId);
  const summary = {
    run: {
      runId,
      profile: profile.name,
      group: groupName,
      caseId,
      legacyEnabled: profile.legacyEnabled,
      outputPolicy,
      requiredCaseIds: selectedCases.map(entry => entry.id),
      startedAt: runStartedAt.toISOString(),
      completedAt: runCompletedAt.toISOString(),
      durationMs: runCompletedAt.getTime() - runStartedAt.getTime(),
      exitCode
    },
    build: buildRecord,
    evidence,
    evidenceError,
    totals,
    cases: caseRecords,
    history,
    host: {
      platform: process.platform,
      arch: process.arch,
      hostname: '<Host>',
      node: process.version
    }
  };

  await writeRunArtifacts(runDir, summary, redactor);
  activeOutput.checkBudget();

  const reportPath = path.join(runDir, 'index.html');
  console.log(`Run report JSON: ${redactor.redact(path.join(runDir, 'summary.json'))}`);
  console.log(`Run report HTML: ${redactor.redact(reportPath)}`);
  if (shouldOpenReport) {
    const opened = await openReportArtifact(reportPath);
    if (opened) {
      console.log(`Opened report: ${redactor.redact(reportPath)}`);
    } else {
      console.warn(`Failed to auto-open report. Open this file manually: ${redactor.redact(reportPath)}`);
    }
  }

  process.exitCode = exitCode;
}

async function openLatest(options) {
  const profileName = options.profile ?? getDefaultProfileName(sourceDir);
  const profile = getProfile(sourceDir, profileName, createProfileOptions(options));
  const redactor = PathRedactor.fromProfile(profile);
  activeRedactor = redactor;
  const paths = getPaths(profile);
  const runDir = await findMostRecentRunDir(paths);
  if (!runDir) {
    throw new Error(`No test runs found under ${redactor.redact(paths.runsRoot)}`);
  }
  const reportPath = path.join(runDir, 'index.html');
  const opened = await openReportArtifact(reportPath);
  if (!opened) {
    throw new Error(`Failed to open report: ${redactor.redact(reportPath)}`);
  }
  console.log(`Opened report: ${redactor.redact(reportPath)}`);
}

async function listRuns(options) {
  const profileName = options.profile ?? getDefaultProfileName(sourceDir);
  const profile = getProfile(sourceDir, profileName, createProfileOptions(options));
  const redactor = PathRedactor.fromProfile(profile);
  activeRedactor = redactor;
  const paths = getPaths(profile);
  let entries = [];
  try {
    entries = await fs.readdir(paths.runsRoot, { withFileTypes: true });
  } catch {
    console.log(`No timestamped test reports found under ${redactor.redact(paths.runsRoot)}`);
    return;
  }

  const runNames = entries
    .filter((entry) => entry.isDirectory())
    .map((entry) => entry.name)
    .sort((left, right) => right.localeCompare(left));

  if (runNames.length === 0) {
    console.log(`No timestamped test reports found under ${redactor.redact(paths.runsRoot)}`);
    return;
  }

  for (const runName of runNames) {
    const summaryPath = path.join(paths.runsRoot, runName, 'summary.json');
    if (!(await pathExists(summaryPath))) {
      continue;
    }
    const summary = await loadJson(summaryPath);
    console.log(`${summary.run.runId} | exit=${summary.run.exitCode} | ${summary.totals.pass} pass / ${summary.totals.fail} fail / ${summary.totals.skip} skip | ${summary.run.group}`);
  }
}

async function listCases(options) {
  const profileName = options.profile ?? getDefaultProfileName(sourceDir);
  const profile = getProfile(sourceDir, profileName, createProfileOptions(options));
  const redactor = PathRedactor.fromProfile(profile);
  activeRedactor = redactor;
  const registry = getTestRegistry(sourceDir, profile);
  const groupName = options.group ?? 'all';
  const selectedCases = getTestSelection(registry, groupName, '');
  if (profile.configAvailable) {
    console.log(`Config: ready (profile: ${profile.name})`);
  } else {
    console.log(`Config: missing (${getConfigRelativePath()})`);
  }
  for (const entry of selectedCases) {
    const configStatus = profile.configAvailable ? 'config: ready' : 'config: missing';
    console.log(`${entry.group.padEnd(12)} ${entry.id.padEnd(32)} ${entry.title} | ${configStatus}`);
  }
}

function buildEffectiveConfigReport(profile, redactor, options) {
  return redactor.redactObject({
    ActiveProfile: profile.name,
    ConfigPath: getConfigRelativePath(),
    Profiles: {
      [profile.name]: {
        GVMRoot: profile.gvmRoot,
        IntegrationRoot: profile.integrationRoot,
        UGLHeadersDir: profile.uglHeadersDir,
        DependencyRoot: profile.dependencyRoot,
        LLVMRoot: profile.llvmRoot,
        Legacy: profile.legacyEnabled,
        BuildDir: profile.buildDir,
        UGLCExecutable: profile.uglcExecutable,
        ClangResourceDir: profile.clangResourceDir,
        HostCompiler: profile.hostCompiler,
        BuildJobs: profile.buildJobs,
        AutoOpenReport: parseBooleanOption(options['open-report'], false)
          && !parseBooleanOption(options['no-open-report'], false),
        LLVMIncludeDirs: profile.llvmIncludeDirs,
        UGLCIncludeDirs: profile.uglcIncludeDirs,
        HostIncludeDirs: profile.hostIncludeDirs,
        RunsRoot: profile.runsRoot
      }
    }
  });
}

async function printEffectiveConfig(options) {
  const profileName = options.profile ?? getDefaultProfileName(sourceDir);
  const profile = getProfile(sourceDir, profileName, createProfileOptions(options));
  const redactor = PathRedactor.fromProfile(profile);
  activeRedactor = redactor;
  console.log(JSON.stringify(buildEffectiveConfigReport(profile, redactor, options), null, 2));
}

async function main() {
  const { command, options } = parseArgs(process.argv);
  if (command === 'help' || options.help) { console.log(outputPolicyHelp); return; }

  if (command === 'run') {
    await runSuite(options);
    return;
  }
  if (command === 'open-latest') {
    await openLatest(options);
    return;
  }
  if (command === 'list-runs') {
    await listRuns(options);
    return;
  }
  if (command === 'list-cases') {
    await listCases(options);
    return;
  }
  if (command === 'print-effective-config') {
    await printEffectiveConfig(options);
    return;
  }

  console.error('Usage: node cli.mjs <run|open-latest|list-runs|list-cases|print-effective-config> [--key value]');
  console.error(outputPolicyHelp);
  process.exitCode = 1;
}

main().catch((error) => {
  const message = error instanceof Error ? error.message : String(error);
  console.error(activeRedactor.redact(message));
  if (isMissingAuthoritativeConfigError(error)) {
    console.error(`Expected config path: ${getConfigRelativePath()}`);
    console.error(`Template: ${getExampleConfigRelativePath()}`);
  }
  process.exitCode = 1;
}).finally(() => activeOutput?.close());
