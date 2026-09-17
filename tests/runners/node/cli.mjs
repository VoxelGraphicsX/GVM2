#!/usr/bin/env node

import { spawn } from 'node:child_process';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

import { describeTestCase } from './test-case-catalog.mjs';
import { getDefaultProfileName, getProfile } from './test-config.mjs';
import { getTestRegistry, getTestSelection } from './test-registry.mjs';
import { inspectGoogleTestReport, validateUglirSelection } from './execution-contract.mjs';
import { captureCompilerEvidence, verifyUnchangedCandidate } from '../shared/compiler-evidence.mjs';
import { ReportRedactor } from '../shared/report-redaction.mjs';

import { parseOutputPolicy, outputPolicyHelp, listReports, RunOutput, RotatingLog, runLoggedCommand } from '../shared/output-policy.mjs';

let activeOutput = null;

const scriptPath = fileURLToPath(import.meta.url);
const scriptDir = path.dirname(scriptPath);
const sourceDir = path.resolve(scriptDir, '..', '..', '..');
let activeRedactor = new ReportRedactor(sourceDir, '');
const renderFeatureLegacyTarget = 'gvm_dsl_render_feature_tests';
const renderFeatureExperimentalTarget = 'gvm_dsl_uglir_render_feature_tests';
const renderFeatureReadbackCases = [
  'Render Case 01 - Procedural Triangle',
  'Render Case 02 - Buffered Triangle',
  'Render Case 03 - Textured Triangle',
  'Render Case 04 - Multi-pass Cube',
  'Render Case 05 - Instanced Triangle Swarm',
  'Render Case 06 - Fullscreen Gradient',
  'Render Case 07 - Compute Pattern Present',
  'Render Case 08 - Offscreen Post Process',
  'Render Case 09 - Pixel Local Deferred Lighting'
];
const renderFeatureReadbackThresholds = {
  meanAbsRgb: 2.0,
  p99AbsRgb: 16,
  largeChannelError: 32,
  largeChannelErrorPixelRatio: 0.005
};

function parseArgs(argv) {
  const [, , command, ...rest] = argv;
  const options = {};
  for (let i = 0; i < rest.length; i += 1) {
    const token = rest[i];
    if (!token.startsWith('--')) {
      continue;
    }
    const key = token.slice(2);
    const next = rest[i + 1];
    if (next && !next.startsWith('--')) {
      options[key] = next;
      i += 1;
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

/** Reads one raw CMake cache value from a configured build directory when the cache already exists. */
async function readCMakeCacheValue(buildDir, key) {
  if (!buildDir) {
    return '';
  }

  const cachePath = path.join(buildDir, 'CMakeCache.txt');
  let content = '';
  try {
    content = await fs.readFile(cachePath, 'utf8');
  } catch {
    return '';
  }

  const prefix = `${key}:`;
  for (const line of content.split(/\r?\n/u)) {
    if (!line.startsWith(prefix)) {
      continue;
    }
    const equalsAt = line.indexOf('=');
    return equalsAt >= 0 ? line.slice(equalsAt + 1).trim() : '';
  }
  return '';
}

/** Returns true when an existing build directory was configured with the UGLIR pipeline enabled. */
async function isLegacyBuildDir(buildDir) {
  const value = (await readCMakeCacheValue(buildDir, 'UGLC_ENABLE_LEGACY')).toUpperCase();
  return value === 'ON' || value === 'TRUE' || value === '1';
}

function normalizeBackendOption(value) {
  const normalized = String(value ?? '').trim().toLowerCase();
  if (normalized === 'metal' || normalized === 'vulkan') {
    return normalized;
  }
  if (normalized.length === 0) {
    return '';
  }
  throw new Error(`Unsupported backend '${value}'. Expected one of: metal, vulkan.`);
}

async function loadText(filePath) {
  return fs.readFile(filePath, 'utf8');
}

async function loadOptionalText(filePath) {
  try {
    return await loadText(filePath);
  } catch {
    return '';
  }
}

async function loadJson(filePath) {
  return JSON.parse(await loadText(filePath));
}

async function writeJson(filePath, value) {
  await fs.mkdir(path.dirname(filePath), { recursive: true });
  await fs.writeFile(filePath, JSON.stringify(activeRedactor.redactObject(value), null, 2));
}

async function ensureCleanDirectory(dirPath) {
  await fs.rm(dirPath, { recursive: true, force: true });
  await fs.mkdir(dirPath, { recursive: true });
}

async function ensureDirectory(dirPath) {
  await fs.mkdir(dirPath, { recursive: true });
}

function sanitizeSegment(value) {
  return String(value)
    .trim()
    .replaceAll(/[^a-zA-Z0-9._-]+/g, '-')
    .replaceAll(/-+/g, '-')
    .replaceAll(/^-|-$/g, '');
}

function makeRunTimestamp(date) {
  const year = date.getFullYear();
  const month = String(date.getMonth() + 1).padStart(2, '0');
  const day = String(date.getDate()).padStart(2, '0');
  const hour = String(date.getHours()).padStart(2, '0');
  const minute = String(date.getMinutes()).padStart(2, '0');
  const second = String(date.getSeconds()).padStart(2, '0');
  const millisecond = String(date.getMilliseconds()).padStart(3, '0');
  return `${year}${month}${day}_${hour}${minute}${second}_${millisecond}`;
}

function buildRunId(startedAt, profileName, groupName, targetName = '') {
  return [
    makeRunTimestamp(startedAt),
    sanitizeSegment(profileName),
    sanitizeSegment(groupName),
    sanitizeSegment(targetName)
  ].filter(Boolean).join('__');
}

function slugify(value) {
  const slug = sanitizeSegment(value).toLowerCase();
  return slug.length > 0 ? slug : 'case';
}

function toRelativeSourcePath(filePath) {
  if (!filePath) {
    return '';
  }
  if (!path.isAbsolute(filePath)) {
    return filePath;
  }
  const relative = path.relative(sourceDir, filePath);
  if (relative.startsWith('..')) {
    return filePath;
  }
  return relative;
}

function toRelativeRunPath(filePath, runDir) {
  if (!filePath) {
    return '';
  }
  return path.relative(runDir, filePath) || '.';
}

function normalizeCommandForDisplay(command, args) {
  return [command, ...args].map((segment) => (
    /[\s"]/u.test(segment)
      ? `"${segment.replaceAll('"', '\\"')}"`
      : segment
  )).join(' ');
}

function truncateForReport(text, maxChars = 12000) {
  const normalized = String(text ?? '');
  if (normalized.length <= maxChars) {
    return normalized;
  }
  return `...<truncated>\n${normalized.slice(-maxChars)}`;
}

async function copyDirectoryIfExists(sourcePath, destinationPath) {
  try {
    await fs.access(sourcePath);
  } catch {
    return [];
  }

  await fs.rm(destinationPath, { recursive: true, force: true });
  await fs.cp(sourcePath, destinationPath, { recursive: true, force: true });

  const copiedFiles = [];
  const stack = [destinationPath];
  while (stack.length > 0) {
    const current = stack.pop();
    const entries = await fs.readdir(current, { withFileTypes: true });
    for (const entry of entries) {
      const entryPath = path.join(current, entry.name);
      if (entry.isDirectory()) {
        stack.push(entryPath);
      } else if (entry.isFile()) {
        copiedFiles.push(entryPath);
      }
    }
  }
  copiedFiles.sort();
  return copiedFiles;
}

/** Returns the same file stem produced by the C++ render feature readback artifact writer. */
function sanitizeRenderReadbackStem(value) {
  return String(value).split('').map((ch) => /[a-zA-Z0-9]/u.test(ch) ? ch : '_').join('');
}

/** Adds the legacy render-feature target when a temporary experimental readback comparison needs it. */
function ensureRenderFeatureReadbackBaseline(selection, registry) {
  const hasExperimentalRenderFeature = selection.some((entry) => entry.target === renderFeatureExperimentalTarget);
  const hasLegacyRenderFeature = selection.some((entry) => entry.target === renderFeatureLegacyTarget);
  if (!hasExperimentalRenderFeature || hasLegacyRenderFeature) {
    return selection;
  }

  const legacyEntry = registry.find((entry) => entry.target === renderFeatureLegacyTarget);
  if (!legacyEntry) {
    return selection;
  }

  const experimentalIndex = selection.findIndex((entry) => entry.target === renderFeatureExperimentalTarget);
  const nextSelection = [...selection];
  nextSelection.splice(experimentalIndex < 0 ? 0 : experimentalIndex, 0, legacyEntry);
  return nextSelection;
}

/** Returns one artifact path for a render-feature readback case and target. */
function getRenderReadbackArtifactPath(runDir, target, caseName, extension) {
  const stem = sanitizeRenderReadbackStem(caseName);
  return path.join(runDir, 'artifacts', target, `${stem}_final.${extension}`);
}

/** Loads one render-feature readback image and its JSON metadata from a test target artifact directory. */
async function loadRenderReadbackArtifact(runDir, target, caseName) {
  const rgbaPath = getRenderReadbackArtifactPath(runDir, target, caseName, 'rgba');
  const jsonPath = getRenderReadbackArtifactPath(runDir, target, caseName, 'json');
  const [pixels, metadata] = await Promise.all([
    fs.readFile(rgbaPath),
    loadJson(jsonPath)
  ]);
  return { rgbaPath, jsonPath, pixels, metadata };
}

/** Calculates byte-level RGB difference metrics between two same-sized RGBA8 readbacks. */
function compareRenderReadbackPixels(legacyPixels, experimentalPixels) {
  const pixelCount = Math.floor(legacyPixels.length / 4);
  const channelErrors = [];
  let totalAbsError = 0;
  let largeErrorPixels = 0;
  let maxAbsError = 0;

  for (let pixelIndex = 0; pixelIndex < pixelCount; pixelIndex += 1) {
    const byteIndex = pixelIndex * 4;
    let pixelHasLargeError = false;
    for (let channel = 0; channel < 3; channel += 1) {
      const error = Math.abs(Number(legacyPixels[byteIndex + channel]) - Number(experimentalPixels[byteIndex + channel]));
      channelErrors.push(error);
      totalAbsError += error;
      maxAbsError = Math.max(maxAbsError, error);
      if (error > renderFeatureReadbackThresholds.largeChannelError) {
        pixelHasLargeError = true;
      }
    }
    if (pixelHasLargeError) {
      largeErrorPixels += 1;
    }
  }

  channelErrors.sort((left, right) => left - right);
  const p99Index = channelErrors.length === 0 ? 0 : Math.min(channelErrors.length - 1, Math.floor(channelErrors.length * 0.99));
  return {
    pixelCount,
    meanAbsRgb: channelErrors.length === 0 ? 0 : totalAbsError / channelErrors.length,
    p99AbsRgb: channelErrors.length === 0 ? 0 : channelErrors[p99Index],
    maxAbsRgb: maxAbsError,
    largeChannelErrorPixels: largeErrorPixels,
    largeChannelErrorPixelRatio: pixelCount === 0 ? 0 : largeErrorPixels / pixelCount
  };
}

/** Returns human-readable parity failures for one render-feature readback comparison. */
function validateRenderReadbackComparison(legacy, experimental, metrics) {
  const failures = [];
  if (legacy.metadata.width !== experimental.metadata.width || legacy.metadata.height !== experimental.metadata.height) {
    failures.push(`Readback dimensions differ: legacy=${legacy.metadata.width}x${legacy.metadata.height}, experimental=${experimental.metadata.width}x${experimental.metadata.height}.`);
  }
  if (legacy.pixels.length !== experimental.pixels.length) {
    failures.push(`Readback byte counts differ: legacy=${legacy.pixels.length}, experimental=${experimental.pixels.length}.`);
  }
  if (metrics.meanAbsRgb > renderFeatureReadbackThresholds.meanAbsRgb) {
    failures.push(`Mean RGB absolute error ${metrics.meanAbsRgb.toFixed(4)} exceeds ${renderFeatureReadbackThresholds.meanAbsRgb}.`);
  }
  if (metrics.p99AbsRgb > renderFeatureReadbackThresholds.p99AbsRgb) {
    failures.push(`P99 RGB absolute error ${metrics.p99AbsRgb} exceeds ${renderFeatureReadbackThresholds.p99AbsRgb}.`);
  }
  if (metrics.largeChannelErrorPixelRatio > renderFeatureReadbackThresholds.largeChannelErrorPixelRatio) {
    failures.push(`Large-error pixel ratio ${metrics.largeChannelErrorPixelRatio.toFixed(6)} exceeds ${renderFeatureReadbackThresholds.largeChannelErrorPixelRatio}.`);
  }

  const legacyNearBlackRatio = Number(legacy.metadata.nearBlackRatio ?? 0);
  const experimentalNearBlackRatio = Number(experimental.metadata.nearBlackRatio ?? 0);
  if (experimentalNearBlackRatio > Math.max(legacyNearBlackRatio + 0.20, legacyNearBlackRatio * 2 + 0.02)) {
    failures.push(`Near-black ratio regressed from ${legacyNearBlackRatio.toFixed(6)} to ${experimentalNearBlackRatio.toFixed(6)}.`);
  }

  const legacyFullyBlackRatio = Number(legacy.metadata.fullyBlackRatio ?? 0);
  const experimentalFullyBlackRatio = Number(experimental.metadata.fullyBlackRatio ?? 0);
  if (experimentalFullyBlackRatio > Math.max(legacyFullyBlackRatio + 0.08, legacyFullyBlackRatio * 2 + 0.01)) {
    failures.push(`Fully-black ratio regressed from ${legacyFullyBlackRatio.toFixed(6)} to ${experimentalFullyBlackRatio.toFixed(6)}.`);
  }

  const legacyDarkTiles = Number(legacy.metadata.darkTileCount ?? 0);
  const experimentalDarkTiles = Number(experimental.metadata.darkTileCount ?? 0);
  if (experimentalDarkTiles > legacyDarkTiles + Math.max(4, Math.ceil(legacyDarkTiles * 0.25))) {
    failures.push(`Dark tile count regressed from ${legacyDarkTiles} to ${experimentalDarkTiles}.`);
  }

  const legacyZeroAlpha = Number(legacy.metadata.zeroAlphaPixels ?? 0);
  const experimentalZeroAlpha = Number(experimental.metadata.zeroAlphaPixels ?? 0);
  const totalPixels = Number(legacy.metadata.totalPixels ?? metrics.pixelCount);
  if (experimentalZeroAlpha > legacyZeroAlpha + Math.max(16, Math.ceil(totalPixels * 0.001))) {
    failures.push(`Zero-alpha pixel count regressed from ${legacyZeroAlpha} to ${experimentalZeroAlpha}.`);
  }
  return failures;
}

/** Writes an amplified RGB absolute-difference preview for a failed readback comparison. */
async function writeRenderReadbackDiffPpm(filePath, width, height, legacyPixels, experimentalPixels) {
  await ensureDirectory(path.dirname(filePath));
  const header = Buffer.from(`P6\n${width} ${height}\n255\n`, 'utf8');
  const body = Buffer.alloc(width * height * 3);
  for (let pixelIndex = 0; pixelIndex < width * height; pixelIndex += 1) {
    const rgbaIndex = pixelIndex * 4;
    const rgbIndex = pixelIndex * 3;
    for (let channel = 0; channel < 3; channel += 1) {
      const error = Math.abs(Number(legacyPixels[rgbaIndex + channel]) - Number(experimentalPixels[rgbaIndex + channel]));
      body[rgbIndex + channel] = Math.min(255, error * 8);
    }
  }
  await fs.writeFile(filePath, Buffer.concat([header, body]));
}

/** Compares all render-feature legacy and experimental final readbacks for the current run. */
async function compareRenderFeatureReadbacks(runDir, backend) {
  const result = {
    transitionalLegacyComparison: true,
    status: 'pass',
    backend: backend || 'default',
    legacyTarget: renderFeatureLegacyTarget,
    experimentalTarget: renderFeatureExperimentalTarget,
    thresholds: renderFeatureReadbackThresholds,
    cases: []
  };
  const diffDir = path.join(runDir, 'artifacts', 'render-feature-readback-diff');

  for (const caseName of renderFeatureReadbackCases) {
    const caseResult = {
      caseName,
      status: 'pass',
      legacy: {},
      experimental: {},
      metrics: {},
      failures: []
    };
    try {
      const [legacy, experimental] = await Promise.all([
        loadRenderReadbackArtifact(runDir, renderFeatureLegacyTarget, caseName),
        loadRenderReadbackArtifact(runDir, renderFeatureExperimentalTarget, caseName)
      ]);
      const sameShape = legacy.metadata.width === experimental.metadata.width
        && legacy.metadata.height === experimental.metadata.height
        && legacy.pixels.length === experimental.pixels.length;
      const metrics = sameShape
        ? compareRenderReadbackPixels(legacy.pixels, experimental.pixels)
        : {
          pixelCount: 0,
          meanAbsRgb: Number.POSITIVE_INFINITY,
          p99AbsRgb: Number.POSITIVE_INFINITY,
          maxAbsRgb: Number.POSITIVE_INFINITY,
          largeChannelErrorPixels: 0,
          largeChannelErrorPixelRatio: 1
        };
      const failures = validateRenderReadbackComparison(legacy, experimental, metrics);
      caseResult.legacy = {
        rgbaPath: toRelativeRunPath(legacy.rgbaPath, runDir),
        jsonPath: toRelativeRunPath(legacy.jsonPath, runDir),
        metadata: legacy.metadata
      };
      caseResult.experimental = {
        rgbaPath: toRelativeRunPath(experimental.rgbaPath, runDir),
        jsonPath: toRelativeRunPath(experimental.jsonPath, runDir),
        metadata: experimental.metadata
      };
      caseResult.metrics = metrics;
      caseResult.failures = failures;
      if (failures.length > 0) {
        caseResult.status = 'fail';
        result.status = 'fail';
        if (sameShape) {
          const diffPath = path.join(diffDir, `${sanitizeRenderReadbackStem(caseName)}_diff.ppm`);
          await writeRenderReadbackDiffPpm(diffPath, legacy.metadata.width, legacy.metadata.height, legacy.pixels, experimental.pixels);
          caseResult.diffPpm = toRelativeRunPath(diffPath, runDir);
        }
      }
    } catch (error) {
      caseResult.status = 'fail';
      caseResult.failures = [`Missing or unreadable render readback artifact: ${error instanceof Error ? error.message : String(error)}`];
      result.status = 'fail';
    }
    result.cases.push(caseResult);
  }

  await writeJson(path.join(diffDir, 'summary.json'), result);
  result.artifactDir = toRelativeRunPath(diffDir, runDir);
  return result;
}

/** Executes a GVM command using the active report's bounded logging policy. */
async function runStreamingCommand(command, args, options = {}) {
  return runLoggedCommand(command, args, { ...options, cwd: options.cwd ?? sourceDir,
    redact: text => activeRedactor.redact(text) }, activeOutput);
}

function toAppleScriptStringLiteral(value) {
  return `"${String(value)
    .replaceAll('\\', '\\\\')
    .replaceAll('"', '\\"')}"`;
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

async function tryOpenWithMacApp(appName, targetPath) {
  return (await runCommand('open', ['-a', appName, targetPath])) === 0;
}

async function tryOpenWithMacDefaultHandler(targetPath) {
  if ((await runCommand('open', [targetPath])) !== 0) {
    return false;
  }

  await runCommand('osascript', [
    '-e', `set reportFile to POSIX file ${toAppleScriptStringLiteral(targetPath)} as alias`,
    '-e', 'set reportApp to (path to default application for reportFile) as text',
    '-e', 'tell application reportApp to activate'
  ]);
  return true;
}

async function openReportArtifact(targetPath) {
  if (process.platform === 'darwin') {
    if (await tryOpenWithMacDefaultHandler(targetPath)) {
      return true;
    }

    const fallbackApps = [
      process.env.GVM_TEST_BROWSER_APP ?? '',
      'Safari',
      'Google Chrome',
      'Firefox'
    ].filter((entry) => entry.length > 0);

    for (const appName of fallbackApps) {
      if (await tryOpenWithMacApp(appName, targetPath)) {
        return true;
      }
    }
    return false;
  }

  if (process.platform === 'win32') {
    return (await runCommand('cmd', ['/c', 'start', '', targetPath])) === 0;
  }

  return (await runCommand('xdg-open', [targetPath])) === 0;
}

function getPaths(profile) {
  const outputRoot = path.join(profile.binaryDir, 'gvm_tests');
  return {
    binaryDir: profile.binaryDir,
    outputRoot,
    runsRoot: path.join(outputRoot, 'runs'),
    binaryRoot: path.join(outputRoot, 'bin'),
    generatedRoot: path.join(outputRoot, 'generated')
  };
}

function applyBuildDirOverride(profile, buildDirOverride) {
  if (!buildDirOverride) {
    return profile;
  }
  return {
    ...profile,
    binaryDir: path.isAbsolute(buildDirOverride)
      ? buildDirOverride
      : path.resolve(sourceDir, buildDirOverride)
  };
}

function getExecutablePath(binaryRoot, target) {
  return path.join(binaryRoot, process.platform === 'win32' ? `${target}.exe` : target);
}

/** Finds the newest complete report regardless of its directory naming mode. */
async function findMostRecentRunDir(paths) {
  const latest = listReports(paths.runsRoot)[0];
  return latest ? path.join(paths.runsRoot, latest.name) : null;
}

function extractFailureMessages(rawFailures) {
  if (rawFailures == null) {
    return [];
  }
  if (typeof rawFailures === 'number') {
    return rawFailures > 0 ? ['Failure details are not available in the raw report.'] : [];
  }
  if (typeof rawFailures === 'string') {
    return rawFailures.length > 0 ? [rawFailures] : [];
  }
  if (Array.isArray(rawFailures)) {
    return rawFailures.flatMap((entry) => extractFailureMessages(entry?.failure ?? entry?.message ?? entry));
  }
  if (typeof rawFailures === 'object') {
    if (typeof rawFailures.failure === 'string') {
      return rawFailures.failure.length > 0 ? [rawFailures.failure] : [];
    }
    return Object.values(rawFailures).flatMap((entry) => extractFailureMessages(entry));
  }
  return [];
}

function toMilliseconds(value) {
  const parsed = Number.parseFloat(value ?? '0');
  return Number.isFinite(parsed) ? parsed * 1000 : 0;
}

function collectCases(node, suitePath = [], cases = [], context = {}) {
  if (Array.isArray(node)) {
    for (const entry of node) {
      collectCases(entry, suitePath, cases, context);
    }
    return cases;
  }

  if (!node || typeof node !== 'object') {
    return cases;
  }

  const nextSuitePath = node.name && node.name !== 'AllTests'
    ? [...suitePath, node.name]
    : suitePath;

  if (Array.isArray(node.testsuites)) {
    collectCases(node.testsuites, nextSuitePath, cases, context);
  }

  if (Array.isArray(node.testsuite)) {
    collectCases(node.testsuite, nextSuitePath, cases, context);
  }

  const hasChildren = Array.isArray(node.testsuite) || Array.isArray(node.testsuites);
  const isLeaf = !hasChildren && typeof node.name === 'string';
  if (!isLeaf) {
    return cases;
  }

  const failures = extractFailureMessages(node.failures);
  const suiteName = node.classname || suitePath.join('.');
  const durationMs = toMilliseconds(node.time);
  const fullName = suiteName ? `${suiteName}.${node.name}` : node.name;
  const sourceFile = node.file ?? '';
  const sourceLine = Number.parseInt(node.line ?? '0', 10) || 0;
  const rawResult = String(node.result ?? '').toUpperCase();
  const status = rawResult === 'SKIPPED'
    ? 'skip'
    : (failures.length > 0 ? 'fail' : 'pass');
  const baseEntry = {
    suite: suiteName || 'unknown',
    name: node.name,
    fullName,
    status,
    durationMs,
    failures,
    sourceFile,
    sourceFileRelative: toRelativeSourcePath(sourceFile),
    sourceLine,
    timestamp: node.timestamp ?? context.timestamp ?? '',
    rawResult: node.result ?? '',
    rawStatus: node.status ?? '',
    reportTarget: context.reportTarget ?? '',
    rawReportFile: context.rawReportFile ?? ''
  };
  const metadata = describeTestCase(baseEntry);
  const caseId = slugify(`${baseEntry.reportTarget}-${baseEntry.fullName}`);
  cases.push({
    ...baseEntry,
    caseId,
    metadata
  });

  return cases;
}

async function loadTargetRecords(runDir) {
  const targetsDir = path.join(runDir, 'targets');
  let entries = [];
  try {
    entries = await fs.readdir(targetsDir, { withFileTypes: true });
  } catch {
    return [];
  }

  const records = [];
  for (const entry of entries) {
    if (!entry.isFile() || !entry.name.endsWith('.json')) {
      continue;
    }
    records.push(await loadJson(path.join(targetsDir, entry.name)));
  }
  records.sort((left, right) => left.target.localeCompare(right.target));
  return records;
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

async function buildSummary(runDir, paths, runMeta) {
  const rawDir = path.join(runDir, 'raw');
  let rawEntries = [];
  try {
    rawEntries = await fs.readdir(rawDir, { withFileTypes: true });
  } catch {
    rawEntries = [];
  }

  const rawFiles = rawEntries
    .filter((entry) => entry.isFile() && entry.name.endsWith('.json'))
    .map((entry) => path.join(rawDir, entry.name))
    .sort();

  const targetRecords = await loadTargetRecords(runDir);
  const targetMap = new Map(targetRecords.map((record) => [record.target, record]));
  const allCases = [];
  const rawReports = [];
  const seenTargets = new Set();

  for (const filePath of rawFiles) {
    let report;
    try { report = await loadJson(filePath); }
    catch { report = {}; }
    const targetName = path.basename(filePath, '.json');
    const targetRecord = targetMap.get(targetName);
    seenTargets.add(targetName);
    rawReports.push({
      fileName: path.basename(filePath),
      filePath: toRelativeRunPath(filePath, runDir),
      targetName
    });

    const cases = collectCases(report, [], [], {
      timestamp: report.timestamp,
      reportTarget: targetName,
      rawReportFile: path.basename(filePath)
    }).map((entry) => ({
      ...entry,
      target: targetRecord ? {
        target: targetRecord.target,
        group: targetRecord.group,
        labels: targetRecord.labels,
        durationMs: targetRecord.durationMs,
        exitCode: targetRecord.exitCode,
        command: targetRecord.command,
        logFile: targetRecord.logFile,
        artifactDir: targetRecord.artifactDir,
        generatedArtifactFiles: targetRecord.generatedArtifactFiles,
        logExcerpt: targetRecord.logExcerpt
      } : null
    }));

    if (cases.length === 0) {
      const fallbackEntry = {
        suite: targetName,
        name: 'execution',
        fullName: `${targetName}.execution`,
        status: 'fail',
        durationMs: targetRecord?.durationMs ?? toMilliseconds(report.time),
        failures: ['No GoogleTest cases were recorded.', targetRecord?.logExcerpt ?? ''],
        sourceFile: '',
        sourceFileRelative: '',
        sourceLine: 0,
        timestamp: report.timestamp ?? '',
        rawResult: '',
        rawStatus: '',
        reportTarget: targetName,
        rawReportFile: path.basename(filePath)
      };
      allCases.push({
        ...fallbackEntry,
        caseId: slugify(`${fallbackEntry.reportTarget}-${fallbackEntry.fullName}`),
        metadata: describeTestCase(fallbackEntry),
        target: targetRecord ? {
          target: targetRecord.target,
          group: targetRecord.group,
          labels: targetRecord.labels,
          durationMs: targetRecord.durationMs,
          exitCode: targetRecord.exitCode,
          command: targetRecord.command,
          logFile: targetRecord.logFile,
          artifactDir: targetRecord.artifactDir,
          generatedArtifactFiles: targetRecord.generatedArtifactFiles,
          logExcerpt: targetRecord.logExcerpt
        } : null
      });
    } else {
      allCases.push(...cases);
    }
  }

  for (const targetRecord of targetRecords) {
    if (seenTargets.has(targetRecord.target)) {
      continue;
    }
    const syntheticEntry = {
      suite: targetRecord.target,
      name: 'execution',
      fullName: `${targetRecord.target}.execution`,
      status: 'fail',
      durationMs: targetRecord.durationMs,
      failures: [
          `The test executable did not produce a GoogleTest JSON report and exited with code ${targetRecord.exitCode}.`,
          targetRecord.logExcerpt
        ],
      sourceFile: '',
      sourceFileRelative: '',
      sourceLine: 0,
      timestamp: targetRecord.completedAt,
      rawResult: '',
      rawStatus: '',
      reportTarget: targetRecord.target,
      rawReportFile: ''
    };
    allCases.push({
      ...syntheticEntry,
      caseId: slugify(`${syntheticEntry.reportTarget}-${syntheticEntry.fullName}`),
      metadata: describeTestCase(syntheticEntry),
      target: {
        target: targetRecord.target,
        group: targetRecord.group,
        labels: targetRecord.labels,
        durationMs: targetRecord.durationMs,
        exitCode: targetRecord.exitCode,
        command: targetRecord.command,
        logFile: targetRecord.logFile,
        artifactDir: targetRecord.artifactDir,
        generatedArtifactFiles: targetRecord.generatedArtifactFiles,
        logExcerpt: targetRecord.logExcerpt
      }
    });
  }

  allCases.sort((a, b) => {
    if (a.status !== b.status) {
      const order = { fail: 0, skip: 1, pass: 2 };
      return (order[a.status] ?? 99) - (order[b.status] ?? 99);
    }
    if (a.durationMs !== b.durationMs) {
      return b.durationMs - a.durationMs;
    }
    return a.fullName.localeCompare(b.fullName);
  });

  const targetSummaries = targetRecords.map((record) => {
    const cases = allCases.filter((entry) => entry.reportTarget === record.target);
    const failCount = cases.filter((entry) => entry.status === 'fail').length;
    const passCount = cases.filter((entry) => entry.status === 'pass').length;
    const skipCount = cases.filter((entry) => entry.status === 'skip').length;
    return {
      ...record,
      caseCount: cases.length,
      passCount,
      failCount,
      skipCount
    };
  });

  const totals = {
    pass: allCases.filter((entry) => entry.status === 'pass').length,
    fail: allCases.filter((entry) => entry.status === 'fail').length,
    skip: allCases.filter((entry) => entry.status === 'skip').length,
    total: allCases.length
  };

  const stepFailures = runMeta.steps.filter((step) => step.status === 'fail').length;
  const targetFailures = targetSummaries.filter((target) => target.exitCode !== 0).length;

  return {
    generatedAt: new Date().toISOString(),
    evidence: runMeta.evidence ?? null,
    evidenceError: runMeta.evidenceError ?? '',
    run: {
      runId: runMeta.runId,
      profile: runMeta.profile,
      group: runMeta.group,
      backend: runMeta.backend,
      legacyEnabled: runMeta.legacyEnabled,
      outputPolicy: activeOutput.policy,
      requiredTargets: runMeta.requiredTargets ?? [],
      startedAt: runMeta.startedAt,
      completedAt: runMeta.completedAt,
      runDirectory: toRelativeRunPath(runDir, sourceDir),
      binaryDirectory: toRelativeRunPath(paths.binaryDir, sourceDir),
      exitCode: runMeta.exitCode,
      host: '<Host>',
      platform: process.platform,
      arch: process.arch
    },
    totals,
    stepFailures,
    targetFailures,
    steps: runMeta.steps,
    rawReports,
    targets: targetSummaries,
    history: await collectRecentRuns(paths, runMeta.runId),
    renderFeatureReadbackComparisons: runMeta.renderFeatureReadbackComparisons ?? [],
    cases: allCases
  };
}

function escapeHtml(value) {
  return String(value)
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#39;');
}

function renderList(items, emptyText) {
  if (!items || items.length === 0) {
    return `<p class="empty-text">${escapeHtml(emptyText)}</p>`;
  }
  return `<ul>${items.map((item) => `<li>${escapeHtml(item)}</li>`).join('')}</ul>`;
}

function renderHistory(summary) {
  const historyEntries = summary.history ?? [];

  return `
    <details class="history-panel">
      <summary class="history-panel-summary">
        <span class="history-panel-title">History</span>
        <span class="history-panel-meta">${historyEntries.length} reports</span>
      </summary>
      <div class="history-list">
      ${historyEntries.length === 0
        ? '<p class="empty-text">No earlier timestamped reports are available yet.</p>'
        : historyEntries.map((entry) => `
        <details class="history-item">
          <summary class="history-item-summary">
            <span class="history-item-name">${escapeHtml(entry.runId)}</span>
            <span class="status-pill ${entry.exitCode === 0 ? 'status-pass' : 'status-fail'}">${entry.exitCode === 0 ? 'PASS' : 'FAIL'}</span>
            <span class="history-compact-metric">${entry.totals.pass} passed / ${entry.totals.total} total</span>
          </summary>
          <div class="history-item-body">
            <div class="history-item-meta">
              <span><strong>Profile</strong>: ${escapeHtml(entry.profile)}</span>
              <span><strong>Group</strong>: ${escapeHtml(entry.group)}</span>
              <span><strong>Generated</strong>: ${escapeHtml(entry.generatedAt)}</span>
              <span><strong>Passed</strong>: ${entry.totals.pass}</span>
              <span><strong>Failed</strong>: ${entry.totals.fail}</span>
              <span><strong>Skipped</strong>: ${entry.totals.skip ?? 0}</span>
              <span><strong>Total</strong>: ${entry.totals.total}</span>
            </div>
            <a class="history-open-link" href="${escapeHtml(entry.href)}">Open report</a>
          </div>
        </details>
      `).join('')}
      </div>
    </details>`;
}

function renderStepRows(summary) {
  return summary.steps.map((step) => `
    <tr class="${step.status === 'fail' ? 'status-fail' : 'status-pass'}">
      <td><span class="status-pill ${step.status === 'fail' ? 'status-fail' : 'status-pass'}">${step.status === 'fail' ? 'FAIL' : 'PASS'}</span></td>
      <td>${escapeHtml(step.name)}</td>
      <td>${step.durationMs.toFixed(2)}</td>
      <td>${escapeHtml(step.command)}</td>
      <td>${escapeHtml(step.logFile)}</td>
    </tr>
  `).join('');
}

function renderTargetRows(summary) {
  if (summary.targets.length === 0) {
    return `
      <tr>
        <td colspan="7">
          <p class="empty-text">No test executables were launched in this run.</p>
        </td>
      </tr>`;
  }

  return summary.targets.map((target) => `
    <tr class="${target.exitCode === 0 ? 'status-pass' : 'status-fail'}">
      <td><span class="status-pill ${target.exitCode === 0 ? 'status-pass' : 'status-fail'}">${target.exitCode === 0 ? 'PASS' : 'FAIL'}</span></td>
      <td>${escapeHtml(target.target)}</td>
      <td>${escapeHtml(target.group)}</td>
      <td>${escapeHtml(target.labels.join(', '))}</td>
      <td>${target.caseCount}</td>
      <td>${target.durationMs.toFixed(2)}</td>
      <td>${escapeHtml(target.logFile)}</td>
    </tr>
  `).join('');
}

/** Renders the transitional beta diagnostic section for render-feature legacy-vs-experimental readback parity. */
function renderRenderFeatureReadbackRows(summary) {
  const comparisons = summary.renderFeatureReadbackComparisons ?? [];
  if (comparisons.length === 0) {
    return '';
  }

  return comparisons.map((comparison) => {
    if (comparison.status === 'skip') {
      return `<section class="section"><h2 class="section-title">Render Feature Readback Parity — SKIP</h2><p>${escapeHtml(comparison.reason)}</p></section>`;
    }
    const rows = comparison.cases.map((entry) => `
    <tr class="${entry.status === 'pass' ? 'status-pass' : 'status-fail'}">
      <td><span class="status-pill ${entry.status === 'pass' ? 'status-pass' : 'status-fail'}">${entry.status === 'pass' ? 'PASS' : 'FAIL'}</span></td>
      <td>${escapeHtml(entry.caseName)}</td>
      <td>${Number(entry.metrics.meanAbsRgb ?? 0).toFixed(4)}</td>
      <td>${Number(entry.metrics.p99AbsRgb ?? 0).toFixed(2)}</td>
      <td>${Number(entry.metrics.largeChannelErrorPixelRatio ?? 0).toFixed(6)}</td>
      <td>${escapeHtml((entry.failures ?? []).join(' | '))}</td>
      <td>${escapeHtml(entry.diffPpm ?? '')}</td>
    </tr>
  `).join('');

    return `
    <section class="section">
      <h2 class="section-title">Render Feature Readback Parity (${escapeHtml(comparison.backend)})</h2>
      <p class="empty-text">Transitional beta diagnostic comparison between ${escapeHtml(comparison.legacyTarget)} and ${escapeHtml(comparison.experimentalTarget)}. This section is intended for UGLIR rollout validation and should be retired or moved to offline diagnostics after the experimental path becomes the default.</p>
      <div class="table-shell">
        <table>
          <thead>
            <tr>
              <th>Status</th>
              <th>Case</th>
              <th>Mean RGB Error</th>
              <th>P99 RGB Error</th>
              <th>Large Error Ratio</th>
              <th>Failures</th>
              <th>Diff</th>
            </tr>
          </thead>
          <tbody>
            ${rows}
          </tbody>
        </table>
      </div>
    </section>`;
  }).join('');
}

function renderHtml(summary) {
  const rows = summary.cases.length === 0
    ? `
      <tr>
        <td colspan="6">
          <p class="empty-text">This run produced no parsed GoogleTest cases. Inspect the configure/build/test logs in this run directory for the failure source.</p>
        </td>
      </tr>`
    : summary.cases.map((entry, index) => {
      const failureBlock = entry.failures.length > 0
        ? `<pre>${escapeHtml(entry.failures.join('\n\n'))}</pre>`
        : '<p class="empty-text">No failure details. The case completed successfully.</p>';
      const cssClass = entry.status === 'fail' ? 'status-fail' : (entry.status === 'skip' ? 'status-skip' : 'status-pass');
      const statusLabel = entry.status === 'fail' ? 'FAIL' : (entry.status === 'skip' ? 'SKIP' : 'PASS');
      const target = entry.target ?? {};
      const searchText = [
        entry.fullName,
        entry.suite,
        entry.name,
        entry.metadata.subsystem,
        entry.metadata.kind,
        entry.metadata.description,
        entry.metadata.content,
        Array.isArray(entry.metadata.requirements) ? entry.metadata.requirements.join(' ') : '',
        Array.isArray(entry.metadata.platformStrategy) ? entry.metadata.platformStrategy.join(' ') : '',
        target.target ?? '',
        target.group ?? '',
        Array.isArray(target.labels) ? target.labels.join(' ') : ''
      ].join(' ').toLowerCase();

      return `
        <tr class="summary-row ${cssClass}" data-detail-row="detail-${index}" data-status="${escapeHtml(entry.status)}" data-search="${escapeHtml(searchText)}">
          <td class="toggle-cell">
            <button type="button" class="toggle-button" aria-expanded="false" aria-controls="detail-${index}">+</button>
          </td>
          <td><span class="status-pill ${cssClass}">${statusLabel}</span></td>
          <td>${escapeHtml(entry.suite)}</td>
          <td>${escapeHtml(entry.name)}</td>
          <td>${escapeHtml(entry.metadata.subsystem)}</td>
          <td>${entry.durationMs.toFixed(2)}</td>
        </tr>
        <tr class="detail-row" id="detail-${index}" data-parent-status="${escapeHtml(entry.status)}">
          <td colspan="6">
            <div class="detail-grid">
              <section class="detail-card">
                <h3>Description</h3>
                <p>${escapeHtml(entry.metadata.description)}</p>
              </section>
              <section class="detail-card">
                <h3>Test Content</h3>
                <p>${escapeHtml(entry.metadata.content)}</p>
              </section>
              <section class="detail-card">
                <h3>Requirements</h3>
                ${renderList(entry.metadata.requirements ?? entry.metadata.validates, 'No explicit execution requirements were registered for this case.')}
              </section>
              <section class="detail-card">
                <h3>Classification</h3>
                <dl class="detail-kv">
                  <div><dt>Subsystem</dt><dd>${escapeHtml(entry.metadata.subsystem)}</dd></div>
                  <div><dt>Level</dt><dd>${escapeHtml(entry.metadata.level)}</dd></div>
                  <div><dt>Kind</dt><dd>${escapeHtml(entry.metadata.kind)}</dd></div>
                  <div><dt>Raw Target</dt><dd>${escapeHtml(entry.reportTarget)}</dd></div>
                </dl>
              </section>
              <section class="detail-card">
                <h3>Validation Focus</h3>
                ${renderList(entry.metadata.validates, 'No explicit validation checklist.')}
              </section>
              <section class="detail-card">
                <h3>Watchouts</h3>
                ${renderList(entry.metadata.watchouts, 'No additional watchouts recorded.')}
              </section>
              <section class="detail-card">
                <h3>Observability</h3>
                ${renderList(entry.metadata.observability, 'This case currently relies on the GoogleTest pass/fail contract and on-disk artifacts.')}
              </section>
              <section class="detail-card">
                <h3>Source</h3>
                <dl class="detail-kv">
                  <div><dt>Case</dt><dd>${escapeHtml(entry.fullName)}</dd></div>
                  <div><dt>File</dt><dd>${escapeHtml(entry.sourceFileRelative || 'Unavailable')}</dd></div>
                  <div><dt>Line</dt><dd>${escapeHtml(entry.sourceLine || '0')}</dd></div>
                  <div><dt>Timestamp</dt><dd>${escapeHtml(entry.timestamp || 'Unavailable')}</dd></div>
                </dl>
              </section>
              <section class="detail-card">
                <h3>Execution Context</h3>
                <dl class="detail-kv">
                  <div><dt>Target</dt><dd>${escapeHtml(target.target || 'Unavailable')}</dd></div>
                  <div><dt>Group</dt><dd>${escapeHtml(target.group || 'Unavailable')}</dd></div>
                  <div><dt>Labels</dt><dd>${escapeHtml(Array.isArray(target.labels) ? target.labels.join(', ') : 'Unavailable')}</dd></div>
                  <div><dt>Exit Code</dt><dd>${escapeHtml(target.exitCode ?? 'Unavailable')}</dd></div>
                  <div><dt>Artifacts</dt><dd>${escapeHtml(target.artifactDir || 'Unavailable')}</dd></div>
                </dl>
              </section>
              <section class="detail-card">
                <h3>Generated Artifacts</h3>
                ${renderList(target.generatedArtifactFiles || [], 'No copied per-run generated artifacts were recorded for this case.')}
              </section>
              <section class="detail-card">
                <h3>Platform Strategy</h3>
                ${renderList(entry.metadata.platformStrategy, 'Desktop execution is active. The same target is intended to stay single-host so mobile can later reuse it as one app with multiple cases.')}
              </section>
              <section class="detail-card detail-card-wide">
                <h3>Failure Details</h3>
                ${failureBlock}
              </section>
            </div>
          </td>
        </tr>`;
    }).join('\n');

  return `<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>GVM Test Report</title>
  <style>
    :root {
      --bg: #07111f;
      --panel: rgba(10, 19, 33, 0.92);
      --panel-strong: rgba(7, 14, 24, 0.96);
      --text: #e5edf6;
      --muted: #90a7bf;
      --line: rgba(120, 142, 167, 0.2);
      --pass: #14532d;
      --pass-soft: rgba(20, 83, 45, 0.22);
      --fail: #991b1b;
      --fail-soft: rgba(153, 27, 27, 0.24);
      --skip: #9a6700;
      --skip-soft: rgba(154, 103, 0, 0.22);
      --accent: #4cc9f0;
      --accent-soft: rgba(76, 201, 240, 0.12);
      --shadow: 0 22px 48px rgba(0, 0, 0, 0.28);
    }
    * {
      box-sizing: border-box;
    }
    body {
      margin: 0;
      font-family: "SF Mono", "Menlo", monospace;
      color: var(--text);
      background:
        radial-gradient(circle at top left, rgba(76, 201, 240, 0.18), transparent 38%),
        radial-gradient(circle at top right, rgba(153, 27, 27, 0.18), transparent 30%),
        linear-gradient(180deg, #091220 0%, #040912 100%);
    }
    main {
      max-width: 1460px;
      margin: 0 auto;
      padding: 28px 24px 40px;
    }
    .hero {
      display: grid;
      grid-template-columns: minmax(0, 2fr) minmax(320px, 1fr);
      gap: 18px;
      margin-bottom: 22px;
    }
    .hero-card,
    .card,
    .table-shell {
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 18px;
      box-shadow: var(--shadow);
    }
    .hero-card {
      padding: 20px 22px;
    }
    .hero-card h1 {
      margin: 0 0 10px;
      font-size: 26px;
    }
    .hero-meta {
      display: grid;
      gap: 6px;
      color: var(--muted);
      line-height: 1.55;
      font-size: 13px;
    }
    .summary {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(170px, 1fr));
      gap: 14px;
      margin-bottom: 18px;
    }
    .card {
      padding: 18px;
    }
    .card h2 {
      margin: 0 0 8px;
      font-size: 13px;
      text-transform: uppercase;
      letter-spacing: 0.06em;
      color: var(--muted);
    }
    .metric {
      font-size: 30px;
      font-weight: 700;
      line-height: 1;
    }
    .controls {
      display: flex;
      flex-wrap: wrap;
      gap: 12px;
      align-items: center;
      margin-bottom: 14px;
    }
    .controls input[type="search"] {
      min-width: 300px;
      flex: 1 1 360px;
      padding: 12px 14px;
      border-radius: 12px;
      border: 1px solid var(--line);
      color: var(--text);
      background: var(--panel-strong);
      font: inherit;
    }
    .controls label {
      display: inline-flex;
      align-items: center;
      gap: 8px;
      padding: 11px 13px;
      border-radius: 12px;
      border: 1px solid var(--line);
      background: var(--panel-strong);
      color: var(--muted);
    }
    .controls button {
      padding: 11px 14px;
      border-radius: 12px;
      border: 1px solid var(--line);
      background: var(--accent-soft);
      color: var(--text);
      font: inherit;
      cursor: pointer;
    }
    .case-counter {
      margin-left: auto;
      padding: 11px 13px;
      border-radius: 12px;
      border: 1px solid var(--line);
      background: var(--panel-strong);
      color: var(--muted);
      font-size: 13px;
      white-space: nowrap;
    }
    .section {
      margin-bottom: 18px;
    }
    .section-title {
      margin: 0 0 12px;
      font-size: 15px;
      text-transform: uppercase;
      letter-spacing: 0.08em;
      color: var(--muted);
    }
    .table-shell {
      overflow: hidden;
    }
    table {
      width: 100%;
      border-collapse: collapse;
    }
    th, td {
      padding: 14px 16px;
      border-bottom: 1px solid var(--line);
      text-align: left;
      vertical-align: top;
    }
    th {
      background: rgba(4, 9, 18, 0.98);
      color: var(--muted);
      font-size: 12px;
      text-transform: uppercase;
      letter-spacing: 0.06em;
    }
    .summary-row {
      cursor: pointer;
      transition: background 140ms ease;
    }
    .summary-row:hover {
      background: rgba(76, 201, 240, 0.08);
    }
    .summary-row.status-fail {
      background: var(--fail-soft);
    }
    .summary-row.status-pass {
      background: var(--pass-soft);
    }
    .summary-row.status-skip {
      background: var(--skip-soft);
    }
    tr.status-fail {
      background: var(--fail-soft);
    }
    tr.status-pass {
      background: var(--pass-soft);
    }
    tr.status-skip {
      background: var(--skip-soft);
    }
    .toggle-cell {
      width: 52px;
    }
    .toggle-button {
      width: 30px;
      height: 30px;
      border-radius: 999px;
      border: 1px solid var(--line);
      background: rgba(255, 255, 255, 0.04);
      color: var(--text);
      cursor: pointer;
      font: inherit;
      font-weight: 700;
    }
    .status-pill {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      min-width: 68px;
      padding: 6px 10px;
      border-radius: 999px;
      font-size: 12px;
      font-weight: 700;
      letter-spacing: 0.04em;
    }
    .status-pill.status-fail {
      background: rgba(185, 28, 28, 0.22);
      color: #fecaca;
    }
    .status-pill.status-pass {
      background: rgba(22, 101, 52, 0.22);
      color: #bbf7d0;
    }
    .status-pill.status-skip {
      background: rgba(154, 103, 0, 0.2);
      color: #fde68a;
    }
    .detail-row {
      display: none;
      background: rgba(4, 9, 18, 0.96);
    }
    .detail-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
      gap: 14px;
    }
    .detail-card {
      padding: 14px;
      border-radius: 14px;
      border: 1px solid var(--line);
      background: rgba(9, 18, 32, 0.95);
    }
    .detail-card-wide {
      grid-column: 1 / -1;
    }
    .detail-card h3 {
      margin: 0 0 10px;
      font-size: 13px;
      color: var(--accent);
      text-transform: uppercase;
      letter-spacing: 0.05em;
    }
    .detail-card p,
    .detail-card li,
    .detail-card dd {
      color: var(--text);
      line-height: 1.6;
    }
    .detail-card ul {
      margin: 0;
      padding-left: 20px;
    }
    .detail-kv {
      margin: 0;
      display: grid;
      gap: 8px;
    }
    .detail-kv div {
      display: grid;
      grid-template-columns: 110px 1fr;
      gap: 10px;
    }
    .detail-kv dt {
      color: var(--muted);
    }
    .detail-kv dd {
      margin: 0;
      overflow-wrap: anywhere;
    }
    .empty-text {
      margin: 0;
      color: var(--muted);
    }
    pre {
      margin: 0;
      white-space: pre-wrap;
      overflow-wrap: anywhere;
      color: #dbeafe;
      background: rgba(3, 7, 13, 0.95);
      padding: 14px;
      border-radius: 12px;
      border: 1px solid var(--line);
      line-height: 1.55;
    }
    details summary {
      cursor: pointer;
      color: var(--accent);
    }
    details summary::-webkit-details-marker {
      display: none;
    }
    .history-panel {
      border: 1px solid var(--line);
      border-radius: 16px;
      background: rgba(9, 18, 32, 0.95);
      overflow: hidden;
    }
    .history-panel-summary {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 16px;
      padding: 16px 18px;
      color: var(--text);
      list-style: none;
    }
    .history-panel-title {
      font-size: 18px;
      font-weight: 700;
      color: var(--text);
    }
    .history-panel-meta {
      color: var(--muted);
      font-size: 12px;
      text-transform: uppercase;
      letter-spacing: 0.06em;
    }
    .history-panel[open] .history-panel-summary {
      border-bottom: 1px solid var(--line);
    }
    .history-list {
      display: grid;
      gap: 10px;
      padding: 14px;
    }
    .history-item {
      border-radius: 14px;
      border: 1px solid var(--line);
      background: rgba(9, 18, 32, 0.95);
      overflow: hidden;
    }
    .history-item-summary {
      display: flex;
      flex-wrap: wrap;
      gap: 10px 14px;
      align-items: center;
      padding: 14px 16px;
      color: var(--text);
      list-style: none;
    }
    .history-item-meta {
      display: grid;
      gap: 8px;
      color: var(--muted);
      font-size: 12px;
    }
    .history-item-body {
      display: grid;
      gap: 12px;
      padding: 0 16px 16px;
      border-top: 1px solid var(--line);
    }
    .history-item-name {
      font-weight: 700;
      color: var(--text);
      overflow-wrap: anywhere;
    }
    .history-compact-metric {
      color: var(--muted);
      font-size: 12px;
    }
    .history-open-link {
      display: inline-flex;
      width: fit-content;
      align-items: center;
      justify-content: center;
      padding: 8px 12px;
      border-radius: 999px;
      border: 1px solid var(--line);
      background: rgba(255, 255, 255, 0.04);
      color: var(--text);
      text-decoration: none;
      font-size: 12px;
      font-weight: 700;
      letter-spacing: 0.04em;
      text-transform: uppercase;
    }
    .history-open-link:hover {
      background: rgba(76, 201, 240, 0.12);
    }
    .hidden-row {
      display: none !important;
    }
    @media (max-width: 980px) {
      .hero {
        grid-template-columns: 1fr;
      }
    }
    @media (max-width: 760px) {
      main {
        padding: 18px 14px 28px;
      }
      th:nth-child(5),
      td:nth-child(5) {
        display: none;
      }
      .controls input[type="search"] {
        min-width: 0;
      }
      .detail-kv div {
        grid-template-columns: 1fr;
      }
    }
  </style>
</head>
<body>
  <main>
    <section class="hero">
      <div class="hero-card">
        <h1>GVM Test Report</h1>
        <div class="hero-meta">
          <div>Run ID: ${escapeHtml(summary.run.runId)}</div>
          <div>Profile: ${escapeHtml(summary.run.profile)} / Group: ${escapeHtml(summary.run.group)}</div>
          <div>Run directory: ${escapeHtml(summary.run.runDirectory)}</div>
          <div>Binary directory: ${escapeHtml(summary.run.binaryDirectory)}</div>
          <div>Started: ${escapeHtml(summary.run.startedAt)}</div>
          <div>Completed: ${escapeHtml(summary.run.completedAt)}</div>
          <div>Generated: ${escapeHtml(summary.generatedAt)}</div>
          <div>Overall exit code: ${escapeHtml(summary.run.exitCode)}</div>
          <div>Host: ${escapeHtml(summary.run.host)} (${escapeHtml(summary.run.platform)} / ${escapeHtml(summary.run.arch)})</div>
        </div>
      </div>
      <div class="hero-card">
        ${renderHistory(summary)}
      </div>
    </section>
    <section class="summary">
      <div class="card">
        <h2>Failed Cases</h2>
        <div class="metric">${summary.totals.fail}</div>
      </div>
      <div class="card">
        <h2>Passed Cases</h2>
        <div class="metric">${summary.totals.pass}</div>
      </div>
      <div class="card">
        <h2>Total Cases</h2>
        <div class="metric">${summary.totals.total}</div>
      </div>
      <div class="card">
        <h2>Skipped Cases</h2>
        <div class="metric">${summary.totals.skip ?? 0}</div>
      </div>
      <div class="card">
        <h2>Failed Targets</h2>
        <div class="metric">${summary.targetFailures}</div>
      </div>
      <div class="card">
        <h2>Failed Steps</h2>
        <div class="metric">${summary.stepFailures}</div>
      </div>
    </section>
    <section class="section">
      <h2 class="section-title">Pipeline Steps</h2>
      <div class="table-shell">
        <table>
          <thead>
            <tr>
              <th>Status</th>
              <th>Step</th>
              <th>Duration (ms)</th>
              <th>Command</th>
              <th>Log</th>
            </tr>
          </thead>
          <tbody>
            ${renderStepRows(summary)}
          </tbody>
        </table>
      </div>
    </section>
    <section class="section">
      <h2 class="section-title">Executable Targets</h2>
      <div class="table-shell">
        <table>
          <thead>
            <tr>
              <th>Status</th>
              <th>Target</th>
              <th>Group</th>
              <th>Labels</th>
              <th>Cases</th>
              <th>Duration (ms)</th>
              <th>Log</th>
            </tr>
          </thead>
          <tbody>
            ${renderTargetRows(summary)}
          </tbody>
        </table>
      </div>
    </section>
    ${renderRenderFeatureReadbackRows(summary)}
    <section class="controls">
      <input id="search-input" type="search" placeholder="Search by suite, case, target, subsystem, or description" />
      <label>
        <input id="failed-only" type="checkbox" />
        Show failed only
      </label>
      <button id="expand-all" type="button">Expand all</button>
      <button id="collapse-all" type="button">Collapse all</button>
      <div id="case-counter" class="case-counter">Showing ${summary.cases.length} of ${summary.cases.length} cases</div>
    </section>
    <section class="section">
      <h2 class="section-title">Parsed Test Cases</h2>
      <div class="table-shell">
        <table>
          <thead>
            <tr>
              <th></th>
              <th>Status</th>
              <th>Suite</th>
              <th>Case</th>
              <th>Subsystem</th>
              <th>Duration (ms)</th>
            </tr>
          </thead>
          <tbody>
            ${rows}
          </tbody>
        </table>
      </div>
    </section>
  </main>
  <script>
    const searchInput = document.getElementById('search-input');
    const failedOnlyCheckbox = document.getElementById('failed-only');
    const expandAllButton = document.getElementById('expand-all');
    const collapseAllButton = document.getElementById('collapse-all');
    const caseCounter = document.getElementById('case-counter');
    const summaryRows = Array.from(document.querySelectorAll('.summary-row'));

    function setExpanded(row, expanded) {
      const detailId = row.getAttribute('data-detail-row');
      const detailRow = document.getElementById(detailId);
      const button = row.querySelector('.toggle-button');
      detailRow.style.display = expanded ? 'table-row' : 'none';
      button.setAttribute('aria-expanded', expanded ? 'true' : 'false');
      button.textContent = expanded ? '−' : '+';
    }

    function applyFilters() {
      const searchText = searchInput.value.trim().toLowerCase();
      const failedOnly = failedOnlyCheckbox.checked;
      let visibleCount = 0;

      summaryRows.forEach((row) => {
        const detailId = row.getAttribute('data-detail-row');
        const detailRow = document.getElementById(detailId);
        const haystack = row.getAttribute('data-search') || '';
        const status = row.getAttribute('data-status');
        const matchesSearch = searchText.length === 0 || haystack.includes(searchText);
        const matchesStatus = !failedOnly || status === 'fail';
        const visible = matchesSearch && matchesStatus;

        row.classList.toggle('hidden-row', !visible);
        detailRow.classList.toggle('hidden-row', !visible);
        if (!visible) {
          setExpanded(row, false);
        } else {
          visibleCount += 1;
        }
      });

      caseCounter.textContent = 'Showing ' + visibleCount + ' of ${summary.cases.length} cases';
    }

    summaryRows.forEach((row) => {
      const button = row.querySelector('.toggle-button');
      const toggle = () => {
        const expanded = button.getAttribute('aria-expanded') === 'true';
        setExpanded(row, !expanded);
      };
      row.addEventListener('click', (event) => {
        if (event.target instanceof HTMLButtonElement) {
          return;
        }
        toggle();
      });
      button.addEventListener('click', (event) => {
        event.stopPropagation();
        toggle();
      });
    });

    searchInput.addEventListener('input', applyFilters);
    failedOnlyCheckbox.addEventListener('change', applyFilters);
    expandAllButton.addEventListener('click', () => {
      summaryRows.forEach((row) => {
        if (!row.classList.contains('hidden-row')) {
          setExpanded(row, true);
        }
      });
    });
    collapseAllButton.addEventListener('click', () => {
      summaryRows.forEach((row) => setExpanded(row, false));
    });

    applyFilters();
  </script>
</body>
</html>`;
}

async function writeReportArtifacts(runDir, summary) {
  summary = activeRedactor.redactObject(summary);
  const casesDir = path.join(runDir, 'cases');
  await ensureCleanDirectory(casesDir);

  for (const entry of summary.cases) {
    await fs.writeFile(path.join(casesDir, `${entry.caseId}.json`), JSON.stringify(entry, null, 2));
  }

  await fs.writeFile(path.join(runDir, 'summary.json'), JSON.stringify(summary, null, 2));
  await fs.writeFile(path.join(runDir, 'index.html'), renderHtml(summary));
}

async function recordTarget(runDir, record) {
  await writeJson(path.join(runDir, 'targets', `${record.target}.json`), record);
}

async function runOneTestTarget(entry, paths, runDir) {
  const executablePath = getExecutablePath(paths.binaryRoot, entry.target);
  const rawFile = path.join(runDir, 'raw', `${entry.target}.json`);
  const logFile = path.join(activeOutput.logDir, `${entry.target}.log`);
  const artifactDir = path.join(runDir, 'artifacts', entry.target);
  const args = [...(entry.args ?? []), `--gtest_output=json:${rawFile}`];
  if (entry.target === renderFeatureLegacyTarget || entry.target === renderFeatureExperimentalTarget) {
    args.push('--artifact-dir', artifactDir, '--write-images', String(activeOutput.policy.writeImages));
  }
  const command = normalizeCommandForDisplay(executablePath, args);
  const startedAt = new Date();

  await ensureDirectory(path.dirname(rawFile));
  await ensureDirectory(path.dirname(logFile));
  await ensureDirectory(artifactDir);

  try {
    await fs.access(executablePath);
  } catch {
    const completedAt = new Date();
    const failureMessage = `Executable not found: ${executablePath}\n`;
    await fs.writeFile(logFile, activeRedactor.redact(failureMessage));
    const record = {
      target: entry.target,
      group: entry.group,
      labels: entry.labels,
      exitCode: 127,
      durationMs: completedAt.getTime() - startedAt.getTime(),
      startedAt: startedAt.toISOString(),
      completedAt: completedAt.toISOString(),
      command,
      logFile: toRelativeRunPath(logFile, runDir),
      artifactDir: toRelativeRunPath(artifactDir, runDir),
      generatedArtifactFiles: [],
      logExcerpt: failureMessage
    };
    await recordTarget(runDir, record);
    return record.exitCode;
  }

  const env = {
    ...entry.environment,
    GTEST_COLOR: 'yes'
  };

  const result = await runStreamingCommand(executablePath, args, {
    cwd: sourceDir,
    env,
    logPath: logFile,
    timeoutMs: entry.timeoutMs,
    inheritOutput: entry.interactiveWindow === true
  });

  let executionCounts = null;
  let reportError = '';
  try {
    executionCounts = inspectGoogleTestReport(await loadJson(rawFile));
    if (executionCounts.failures > 0) result.exitCode ||= 1;
    if (entry.group === 'uglir' && executionCounts.executed === 0) {
      throw new Error('Experimental target executed zero test cases.');
    }
  } catch (error) {
    reportError = `Invalid GoogleTest evidence for ${entry.target}: ${error.message}`;
    result.exitCode ||= 1;
    result.output += `\n${reportError}\n`;
    const log = new RotatingLog(logFile, activeOutput.policy, true);
    try { log.write(activeRedactor.redact(`\n${reportError}\n`)); } finally { log.close(); }
  }

  const generatedArtifactFiles = [];
  if (entry.environment?.GVM_TEST_DSL_GENERATED_DIR) {
    const copied = await copyDirectoryIfExists(
      entry.environment.GVM_TEST_DSL_GENERATED_DIR,
      path.join(artifactDir, 'generated')
    );
    generatedArtifactFiles.push(...copied.map((filePath) => toRelativeRunPath(filePath, runDir)));
  }

  const record = {
    target: entry.target,
    group: entry.group,
    labels: entry.labels,
    exitCode: result.exitCode,
    durationMs: result.durationMs,
    startedAt: result.startedAt.toISOString(),
    completedAt: result.completedAt.toISOString(),
    timedOut: result.timedOut,
    executionCounts,
    reportError,
    command,
    logFile: toRelativeRunPath(logFile, runDir),
    artifactDir: toRelativeRunPath(artifactDir, runDir),
    generatedArtifactFiles,
    logExcerpt: truncateForReport(result.output)
  };
  await recordTarget(runDir, record);
  return result.exitCode;
}

async function configureProject(profile, runDir) {
  const args = [
    '-S', sourceDir,
    '-B', profile.binaryDir,
    '-G', profile.generator,
    `-DCMAKE_BUILD_TYPE=${profile.buildType}`,
    ...Object.entries(profile.cmakeCache).map(([key, value]) => `-D${key}=${value}`)
  ];
  const result = await runStreamingCommand('cmake', args, {
    cwd: sourceDir,
    logPath: path.join(activeOutput.logDir, 'configure.log')
  });
  return {
    name: 'configure',
    status: result.exitCode === 0 ? 'pass' : 'fail',
    durationMs: result.durationMs,
    command: normalizeCommandForDisplay('cmake', args),
    logFile: toRelativeRunPath(path.join(activeOutput.logDir, 'configure.log'), runDir),
    logExcerpt: truncateForReport(result.output),
    exitCode: result.exitCode
  };
}

async function buildProject(profile, runDir) {
  const args = [
    '--build', profile.binaryDir,
    '--parallel', String(profile.buildJobs ?? 4)
  ];
  const result = await runStreamingCommand('cmake', args, {
    cwd: sourceDir,
    logPath: path.join(activeOutput.logDir, 'build.log')
  });
  return {
    name: 'build',
    status: result.exitCode === 0 ? 'pass' : 'fail',
    durationMs: result.durationMs,
    command: normalizeCommandForDisplay('cmake', args),
    logFile: toRelativeRunPath(path.join(activeOutput.logDir, 'build.log'), runDir),
    logExcerpt: truncateForReport(result.output),
    exitCode: result.exitCode
  };
}

async function commandRun(options) {
  const outputPolicy = parseOutputPolicy(options);
  const profileName = options.profile ?? getDefaultProfileName();
  const requestedGroup = options.group ?? 'all';
  const requestedTarget = options.target ?? '';
  const requestedBackend = normalizeBackendOption(options.backend);
  const shouldOpenReport = parseBooleanOption(options['open-report'], false)
    && !parseBooleanOption(options['no-open-report'], false);

  const profile = applyBuildDirOverride(getProfile(sourceDir, profileName), options['build-dir']);
  activeRedactor = new ReportRedactor(sourceDir, profile.binaryDir);
  const paths = getPaths(profile);
  const legacyEnabled = await isLegacyBuildDir(profile.binaryDir);
  const registry = getTestRegistry(sourceDir, profile.binaryDir, {
    backend: requestedBackend,
    includeLegacy: legacyEnabled
  });
  let selection = requestedTarget
    ? registry.filter((entry) => entry.target === requestedTarget)
    : getTestSelection(registry, requestedGroup);
  selection = ensureRenderFeatureReadbackBaseline(selection, registry);

  if (selection.length === 0) {
    throw new Error(`No tests matched profile=${profileName}, group=${requestedGroup}, target=${requestedTarget || '<none>'}.`);
  }
  validateUglirSelection(legacyEnabled, registry, selection, !requestedTarget && (requestedGroup === 'all' || requestedGroup === 'uglir'));
  if (!requestedBackend && selection.some(entry => entry.labels.includes('rhi') && entry.group !== 'unit')) {
    throw new Error('Backend execution requires an explicit --backend metal or --backend vulkan.');
  }

  const startedAt = new Date();
  const runScope = requestedTarget || requestedGroup;
  const runId = buildRunId(startedAt, profileName, requestedBackend ? `${runScope}-${requestedBackend}` : runScope);
  activeOutput = new RunOutput(paths.runsRoot, runId, outputPolicy);
  const runDir = activeOutput.runDir;
  await ensureDirectory(path.join(runDir, 'raw'));
  await ensureDirectory(path.join(runDir, 'logs'));
  await ensureDirectory(path.join(runDir, 'artifacts'));
  await ensureDirectory(path.join(runDir, 'targets'));

  const steps = [];
  const configureStep = await configureProject(profile, runDir);
  steps.push(configureStep);

  let exitCode = configureStep.exitCode;
  if (exitCode === 0 && await isLegacyBuildDir(profile.binaryDir) !== legacyEnabled) {
    throw new Error('Configure changed the Legacy build setting after test selection. Configure explicitly before running the suite.');
  }
  if (exitCode === 0) {
    const buildStep = await buildProject(profile, runDir);
    steps.push(buildStep);
    exitCode = buildStep.exitCode;
  } else {
    steps.push({
      name: 'build',
      status: 'fail',
      durationMs: 0,
      command: 'Skipped because configure failed.',
      logFile: '',
      logExcerpt: 'Build did not run because configure failed.',
      exitCode: configureStep.exitCode
    });
  }

  let evidence = null;
  let evidenceError = '';
  if (exitCode === 0) {
    evidence = await captureCompilerEvidence(sourceDir, profile.binaryDir);
    for (const entry of selection) {
      if (activeOutput.controller.signal.aborted) break;
      const targetExitCode = await runOneTestTarget(entry, paths, runDir);
      if (targetExitCode !== 0) {
        exitCode = targetExitCode;
      }
    }
    try { verifyUnchangedCandidate(evidence, await captureCompilerEvidence(sourceDir, profile.binaryDir)); }
    catch (error) { evidenceError = error.message; exitCode ||= 1; }
  }

  const renderFeatureReadbackComparisons = [];
  const shouldCompareRenderFeatureReadbacks = legacyEnabled && selection.some((entry) => entry.target === renderFeatureExperimentalTarget)
    && selection.some((entry) => entry.target === renderFeatureLegacyTarget);
  if (exitCode === 0 && shouldCompareRenderFeatureReadbacks) {
    const comparison = outputPolicy.writeImages
      ? await compareRenderFeatureReadbacks(runDir, requestedBackend)
      : {
        status: 'skip', backend: requestedBackend, legacyTarget: renderFeatureLegacyTarget,
        experimentalTarget: renderFeatureExperimentalTarget,
        reason: 'Image writing is disabled by --write-images false; file-based pixel comparison was not executed.',
        cases: []
      };
    renderFeatureReadbackComparisons.push(comparison);
    if (comparison.status === 'fail') {
      exitCode = 1;
    }
  }

  const completedAt = new Date();
  if (activeOutput.controller.signal.aborted) {
    exitCode = 130;
    evidenceError = activeOutput.controller.signal.reason.message;
  }
  const summary = await buildSummary(runDir, paths, {
    runId,
    profile: profileName,
    group: requestedTarget || requestedGroup,
    backend: requestedBackend,
    legacyEnabled,
    requiredTargets: selection.map(entry => entry.target),
    evidence,
    evidenceError,
    startedAt: startedAt.toISOString(),
    completedAt: completedAt.toISOString(),
    exitCode,
    steps,
    renderFeatureReadbackComparisons
  });

  await writeReportArtifacts(runDir, summary);
  activeOutput.checkBudget();

  const reportHtmlPath = path.join(runDir, 'index.html');
  console.log(activeRedactor.redact(`Summary written to ${path.join(runDir, 'summary.json')}`));
  console.log(activeRedactor.redact(`Run report HTML: ${reportHtmlPath}`));

  if (shouldOpenReport) {
    const opened = await openReportArtifact(reportHtmlPath);
    if (opened) {
      console.log(activeRedactor.redact(`Opened report: ${reportHtmlPath}`));
    } else {
      console.warn(activeRedactor.redact(`Failed to auto-open report. Open this file manually: ${reportHtmlPath}`));
    }
  } else {
    console.log(activeRedactor.redact(`Auto-open disabled. Report HTML: ${reportHtmlPath}`));
  }

  process.exitCode = exitCode;
}

async function commandOpenLatest(options) {
  const profileName = options.profile ?? getDefaultProfileName();
  const profile = applyBuildDirOverride(getProfile(sourceDir, profileName), options['build-dir']);
  const paths = getPaths(profile);
  const runDir = await findMostRecentRunDir(paths);
  if (!runDir) {
    throw new Error(`No complete run reports were found under ${paths.runsRoot}`);
  }
  const reportPath = path.join(runDir, 'index.html');
  const opened = await openReportArtifact(reportPath);
  if (opened) {
    console.log(`Opened report: ${reportPath}`);
  } else {
    throw new Error(`Failed to open report: ${reportPath}`);
  }
}

async function commandListRuns(options) {
  const profileName = options.profile ?? getDefaultProfileName();
  const profile = applyBuildDirOverride(getProfile(sourceDir, profileName), options['build-dir']);
  const paths = getPaths(profile);
  const history = await collectRecentRuns(paths, '');
  if (history.length === 0) {
    console.log(`No test reports found under ${paths.runsRoot}`);
    return;
  }
  for (const entry of history) {
    console.log(`${entry.runId} | ${entry.profile}/${entry.group} | exit=${entry.exitCode} | fail=${entry.totals.fail}/${entry.totals.total} | ${entry.href}`);
  }
}

async function main() {
  const { command, options } = parseArgs(process.argv);
  if (command === 'help' || options.help) { console.log(outputPolicyHelp); return; }
  if (command === 'run') {
    await commandRun(options);
    return;
  }
  if (command === 'open-latest') {
    await commandOpenLatest(options);
    return;
  }
  if (command === 'list-runs') {
    await commandListRuns(options);
    return;
  }

  console.error('Usage: node cli.mjs <run|open-latest|list-runs> [--key value]');
  console.error(outputPolicyHelp);
  process.exitCode = 1;
}

main().finally(async () => {
  try {
    if (activeOutput) await activeRedactor.redactDirectory(activeOutput.runDir);
  } finally { activeOutput?.close(); }
}).catch((error) => {
  console.error(activeRedactor.redact(error.stack || error.message || String(error)));
  process.exitCode = 1;
});
