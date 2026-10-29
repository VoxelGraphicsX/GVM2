import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const DEFAULT_PROFILE_NAME = 'gvm-local';
const CONFIG_RELATIVE_PATH = path.join('.local', 'UGLC.TestConfig.json');
const EXAMPLE_CONFIG_RELATIVE_PATH = path.join('tests', 'runners', 'uglc', 'node', 'UGLC.TestConfig.example.json');

class UGLCTestConfigError extends Error {
  constructor(message, code = 'UGLC_CONFIG_ERROR') {
    super(message);
    this.name = 'UGLCTestConfigError';
    this.code = code;
  }
}

function maxBuildJobs() {
  const availableParallelism = typeof os.availableParallelism === 'function'
    ? os.availableParallelism()
    : 0;
  const cpuCount = Array.isArray(os.cpus()) ? os.cpus().length : 0;
  return Math.max(1, availableParallelism || cpuCount || 1);
}

function optionalString(value, defaultValue = '') {
  return typeof value === 'string' && value.trim() !== '' ? value.trim() : defaultValue;
}

function optionalBoolean(value, defaultValue) {
  return typeof value === 'boolean' ? value : defaultValue;
}

function parseBuildJobs(value) {
  return Number.isInteger(value) && value > 0 ? value : maxBuildJobs();
}

function resolveConfiguredPath(sourceDir, value, defaultPath) {
  const rawPath = optionalString(value, defaultPath);
  return path.isAbsolute(rawPath) ? path.normalize(rawPath) : path.resolve(sourceDir, rawPath);
}

function readJsonFileIfPresent(filePath) {
  if (!fs.existsSync(filePath)) {
    return null;
  }
  try {
    return JSON.parse(fs.readFileSync(filePath, 'utf8'));
  } catch (error) {
    if (error instanceof SyntaxError) {
      throw new UGLCTestConfigError(`Invalid JSON in ${CONFIG_RELATIVE_PATH}: ${error.message}`);
    }
    throw error;
  }
}

function readCMakeCacheValue(buildDir, key) {
  const cachePath = path.join(buildDir, 'CMakeCache.txt');
  if (!fs.existsSync(cachePath)) {
    return '';
  }
  const cacheText = fs.readFileSync(cachePath, 'utf8');
  const escapedKey = key.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const match = new RegExp(`^${escapedKey}:[^=]*=(.*)$`, 'm').exec(cacheText);
  return match ? match[1].trim() : '';
}

/** Returns true when a CMake cache boolean uses an enabled spelling. */
function readCMakeCacheBoolean(buildDir, key) {
  const normalized = readCMakeCacheValue(buildDir, key).trim().toUpperCase();
  return ['1', 'ON', 'TRUE', 'YES'].includes(normalized);
}

/** Returns only existing directory paths while preserving the first occurrence order. */
function collectExistingDirectories(entries) {
  return entries.filter((entry) => typeof entry === 'string' && entry.length > 0 && fs.existsSync(entry) && fs.statSync(entry).isDirectory());
}

/** Joins a child path to a root directory only when the root is configured. */
function joinConfiguredPath(rootDir, ...segments) {
  return rootDir ? path.join(rootDir, ...segments) : '';
}

/** Finds CPM source-cache package directories that use hashed leaf names. */
function collectNestedPackageDirectories(rootDir, packageName) {
  if (!rootDir || !fs.existsSync(rootDir)) {
    return [];
  }
  const packageRoot = path.join(rootDir, packageName);
  if (!fs.existsSync(packageRoot)) {
    return [];
  }
  return fs.readdirSync(packageRoot)
    .map((entry) => path.join(packageRoot, entry))
    .filter((entryPath) => fs.existsSync(entryPath) && fs.statSync(entryPath).isDirectory())
    .sort();
}

/** Collects include directories from both legacy build/_deps and shared CPM cache layouts. */
function collectDependencyIncludeDirs(buildDir, dependencyRoot) {
  const fetchContentRoot = readCMakeCacheValue(buildDir, 'FETCHCONTENT_BASE_DIR');
  const cpmSourceCache = readCMakeCacheValue(buildDir, 'CPM_SOURCE_CACHE');
  const eastlSourceDir = readCMakeCacheValue(buildDir, 'EASTL_SOURCE_DIR')
    || readCMakeCacheValue(buildDir, 'CPM_PACKAGE_EASTL_SOURCE_DIR');
  const eabaseSourceDir = readCMakeCacheValue(buildDir, 'EABase_SOURCE_DIR')
    || readCMakeCacheValue(buildDir, 'CPM_PACKAGE_EABase_SOURCE_DIR');
  const marlSourceDir = readCMakeCacheValue(buildDir, 'marl_SOURCE_DIR')
    || readCMakeCacheValue(buildDir, 'CPM_PACKAGE_marl_SOURCE_DIR');
  const glmSourceDir = readCMakeCacheValue(buildDir, 'glm_SOURCE_DIR')
    || readCMakeCacheValue(buildDir, 'CPM_PACKAGE_glm_SOURCE_DIR');
  const nlohmannJsonSourceDir = readCMakeCacheValue(buildDir, 'nlohmann_json_SOURCE_DIR')
    || readCMakeCacheValue(buildDir, 'CPM_PACKAGE_nlohmann_json_SOURCE_DIR');

  const includeCandidates = [
    joinConfiguredPath(dependencyRoot, 'glm-src'),
    joinConfiguredPath(dependencyRoot, 'eabase-src', 'include', 'Common'),
    joinConfiguredPath(dependencyRoot, 'eastl-src', 'include'),
    joinConfiguredPath(dependencyRoot, 'marl-src', 'include'),
    joinConfiguredPath(fetchContentRoot, 'glm-src'),
    joinConfiguredPath(fetchContentRoot, 'eabase-src', 'include', 'Common'),
    joinConfiguredPath(fetchContentRoot, 'eastl-src', 'include'),
    joinConfiguredPath(fetchContentRoot, 'marl-src', 'include'),
    glmSourceDir,
    joinConfiguredPath(eabaseSourceDir, 'include', 'Common'),
    joinConfiguredPath(eastlSourceDir, 'include'),
    joinConfiguredPath(marlSourceDir, 'include'),
    joinConfiguredPath(nlohmannJsonSourceDir, 'include')
  ];

  for (const entry of collectNestedPackageDirectories(cpmSourceCache, 'glm')) {
    includeCandidates.push(entry);
  }
  for (const entry of collectNestedPackageDirectories(cpmSourceCache, 'eabase')) {
    includeCandidates.push(path.join(entry, 'include', 'Common'));
  }
  for (const entry of collectNestedPackageDirectories(cpmSourceCache, 'eastl')) {
    includeCandidates.push(path.join(entry, 'include'));
  }
  for (const entry of collectNestedPackageDirectories(cpmSourceCache, 'marl')) {
    includeCandidates.push(path.join(entry, 'include'));
  }
  for (const entry of collectNestedPackageDirectories(cpmSourceCache, 'nlohmann_json')) {
    includeCandidates.push(path.join(entry, 'include'));
  }

  return [...new Set(collectExistingDirectories(includeCandidates))];
}

function isLLVMRoot(candidateRoot) {
  return Boolean(candidateRoot)
    && fs.existsSync(path.join(candidateRoot, 'include', 'clang', 'AST', 'ASTContext.h'))
    && fs.existsSync(path.join(candidateRoot, 'include', 'llvm', 'Support', 'Compiler.h'));
}

function isClangResourceDir(candidateRoot) {
  return Boolean(candidateRoot)
    && fs.existsSync(candidateRoot)
    && fs.existsSync(path.join(candidateRoot, 'include'));
}

function resolveBootstrappedLLVMRoot(buildDir) {
  const cacheDir = readCMakeCacheValue(buildDir, 'UGLC_LLVM_CACHE_DIR')
    || path.join(buildDir, '_deps', 'llvm');
  const toolchainsDir = path.join(cacheDir, 'toolchains');
  if (!fs.existsSync(toolchainsDir)) {
    return '';
  }

  const roots = fs.readdirSync(toolchainsDir)
    .map((entry) => path.join(toolchainsDir, entry))
    .filter(isLLVMRoot)
    .sort();
  return roots.at(-1) ?? '';
}

function resolveLLVMRoot(buildDir, configuredRoot = '') {
  if (isLLVMRoot(configuredRoot)) {
    return path.normalize(configuredRoot);
  }

  const cacheRoot = readCMakeCacheValue(buildDir, 'UGLC_RESOLVED_LLVM_ROOT')
    || readCMakeCacheValue(buildDir, 'UGLC_LLVM_ROOT');
  if (isLLVMRoot(cacheRoot)) {
    return path.normalize(cacheRoot);
  }

  const bootstrappedRoot = resolveBootstrappedLLVMRoot(buildDir);
  if (isLLVMRoot(bootstrappedRoot)) {
    return path.normalize(bootstrappedRoot);
  }

  const cmakeCompiler = readCMakeCacheValue(buildDir, 'CMAKE_CXX_COMPILER');
  if (cmakeCompiler) {
    const compilerRoot = path.dirname(path.dirname(cmakeCompiler));
    if (isLLVMRoot(compilerRoot)) {
      return path.normalize(compilerRoot);
    }
  }

  return '';
}

function resolveClangResourceDir(sourceDir, buildDir, rawProfile = {}) {
  const configuredRoot = optionalString(rawProfile.ClangResourceDir ?? process.env.GVM_UGLC_TEST_RESOURCE_DIR);
  if (configuredRoot) {
    const resolvedRoot = path.isAbsolute(configuredRoot)
      ? path.normalize(configuredRoot)
      : path.resolve(sourceDir, configuredRoot);
    if (isClangResourceDir(resolvedRoot)) {
      return resolvedRoot;
    }
  }

  const cacheRoot = readCMakeCacheValue(buildDir, 'GVM_UGLC_RESOURCE_DIR');
  if (isClangResourceDir(cacheRoot)) {
    return path.normalize(cacheRoot);
  }

  const llvmRoot = resolveLLVMRoot(buildDir, rawProfile.LLVMRoot);
  if (llvmRoot) {
    const clangRoot = path.join(llvmRoot, 'lib', 'clang');
    if (fs.existsSync(clangRoot)) {
      const versions = fs.readdirSync(clangRoot)
        .map((entry) => path.join(clangRoot, entry))
        .filter(isClangResourceDir)
        .sort();
      if (versions.length > 0) {
        return versions.at(-1);
      }
    }
  }

  return '';
}

/** Builds a profile from JSON config and explicit command-line overrides. */
function profileFromRawConfig(sourceDir, profileName, rawProfile = {}, profileOptions = {}) {
  const buildDirOverride = optionalString(profileOptions.buildDirOverride ?? profileOptions['build-dir']);
  const hasBuildDirOverride = buildDirOverride !== '';
  const buildDir = resolveConfiguredPath(
    sourceDir,
    hasBuildDirOverride ? buildDirOverride : (rawProfile.BuildDir ?? process.env.GVM_UGLC_TEST_BUILD_DIR),
    path.join(sourceDir, 'build', 'macos-tests-debug-make')
  );
  const gvmRoot = resolveConfiguredPath(sourceDir, rawProfile.GVMRoot, path.join(sourceDir, 'GVM'));
  const uglHeadersDir = resolveConfiguredPath(sourceDir, rawProfile.UGLHeadersDir, path.join(gvmRoot, 'UGLHeaders'));
  const dependencyRoot = resolveConfiguredPath(
    sourceDir,
    hasBuildDirOverride ? '' : rawProfile.DependencyRoot,
    path.join(buildDir, '_deps')
  );
  const uglcExecutable = resolveConfiguredPath(
    sourceDir,
    hasBuildDirOverride ? '' : rawProfile.UGLCExecutable,
    path.join(buildDir, 'UGLC')
  );
  const buildJobs = parseBuildJobs(rawProfile.BuildJobs);
  const llvmRoot = resolveLLVMRoot(buildDir, rawProfile.LLVMRoot);
  const clangResourceDir = resolveClangResourceDir(sourceDir, buildDir, rawProfile);
  const llvmIncludeDirs = llvmRoot ? [path.join(llvmRoot, 'include')] : [];
  const legacyEnabled = readCMakeCacheBoolean(buildDir, 'UGLC_ENABLE_LEGACY');

  const optionalIncludeDirs = collectDependencyIncludeDirs(buildDir, dependencyRoot);
  const dependencyConfigIncludeDir = path.resolve(gvmRoot, '..', 'cmake', 'dependencies', 'include');

  const uglcIncludeDirs = [
    ...optionalIncludeDirs,
    gvmRoot,
    uglHeadersDir,
    path.join(gvmRoot, 'third_party')
  ];
  const hostIncludeDirs = [
    dependencyConfigIncludeDir,
    gvmRoot,
    uglHeadersDir,
    path.join(gvmRoot, 'third_party'),
    path.join(gvmRoot, 'third_party', 'EASTL_Helper'),
    ...optionalIncludeDirs,
    ...llvmIncludeDirs
  ];

  return {
    name: profileName,
    configAvailable: true,
    configRelativePath: CONFIG_RELATIVE_PATH,
    buildDir,
    uglcExecutable,
    uglHeadersDir,
    uglcIncludeDirs,
    runsRoot: path.join(buildDir, 'uglc_tests', 'runs'),
    buildCommand: ['cmake', '--build', buildDir, '--target', 'UGLC', `-j${buildJobs}`],
    buildTimeoutMs: 10 * 60 * 1000,
    defaultTimeoutMs: 90 * 1000,
    autoOpenReport: optionalBoolean(rawProfile.AutoOpenReport, false),
    hostCompiler: optionalString(rawProfile.HostCompiler, 'c++'),
    // Direct compiler probes must use the same dependency contract as GVM's CMake targets.
    hostCompileBaseArgs: ['-std=c++20', '-include', 'GVM/Dependencies/GLMConfig.h',
      '-DEASTL_USER_CONFIG_HEADER="GVM/Dependencies/EASTLConfig.h"',
      '-DLLVM_DISABLE_ABI_BREAKING_CHECKS_ENFORCING=1', '-fsyntax-only'],
    hostIncludeDirs,
    llvmRoot,
    clangResourceDir,
    llvmIncludeDirs,
    legacyEnabled,
    buildJobs,
    gvmRoot,
    dependencyRoot,
    requiredPathChecks: [
      { fieldPath: 'BuildDir', path: buildDir },
      { fieldPath: 'GVMRoot', path: gvmRoot },
      { fieldPath: 'UGLHeadersDir', path: uglHeadersDir },
      { fieldPath: 'DependencyConfigIncludeDir', path: dependencyConfigIncludeDir },
      { fieldPath: 'UGLC/Source', path: path.join(sourceDir, 'UGLC', 'Source') },
      { fieldPath: 'tests/uglc/fixtures', path: path.join(sourceDir, 'tests', 'uglc', 'fixtures') },
      { fieldPath: 'tests/uglc/harnesses', path: path.join(sourceDir, 'tests', 'uglc', 'harnesses') }
    ],
    redactionRoots: {
      sourceDir,
      GVMRoot: gvmRoot,
      UGLHeadersDir: uglHeadersDir,
      DependencyRoot: dependencyRoot,
      LLVMRoot: llvmRoot,
      ClangResourceDir: clangResourceDir,
      homeDir: os.homedir()
    }
  };
}

export function getConfigPath(sourceDir) {
  return path.join(sourceDir, CONFIG_RELATIVE_PATH);
}

export function getExampleConfigPath(sourceDir) {
  return path.join(sourceDir, EXAMPLE_CONFIG_RELATIVE_PATH);
}

export function getConfigRelativePath() {
  return CONFIG_RELATIVE_PATH;
}

export function getExampleConfigRelativePath() {
  return EXAMPLE_CONFIG_RELATIVE_PATH;
}

export function isMissingAuthoritativeConfigError(error) {
  return error instanceof Error && error.code === 'UGLC_CONFIG_MISSING';
}

export function loadAuthoritativeConfig(sourceDir) {
  const config = readJsonFileIfPresent(getConfigPath(sourceDir));
  if (!config) {
    throw new UGLCTestConfigError(`Missing optional config: ${CONFIG_RELATIVE_PATH}`, 'UGLC_CONFIG_MISSING');
  }
  return config;
}

export function getDefaultProfileName() {
  return DEFAULT_PROFILE_NAME;
}

/** Resolves the effective test profile, applying command-line overrides after local JSON config. */
export function getProfile(sourceDir, requestedProfileName = DEFAULT_PROFILE_NAME, profileOptions = {}) {
  const config = readJsonFileIfPresent(getConfigPath(sourceDir));
  if (!config) {
    return profileFromRawConfig(sourceDir, requestedProfileName || DEFAULT_PROFILE_NAME, {}, profileOptions);
  }

  const activeProfile = optionalString(config.ActiveProfile, DEFAULT_PROFILE_NAME);
  const profileName = requestedProfileName || activeProfile;
  const rawProfile = config.Profiles?.[profileName] ?? {};
  return profileFromRawConfig(sourceDir, profileName, rawProfile, profileOptions);
}
