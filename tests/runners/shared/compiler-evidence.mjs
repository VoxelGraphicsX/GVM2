import { createHash } from 'node:crypto';
import { createReadStream, promises as fs } from 'node:fs';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import os from 'node:os';
import path from 'node:path';

const executeFile = promisify(execFile);
const sourceRoots = ['CMakeLists.txt', 'cmake', '.github', 'UGLC/Source', 'GVM', 'tests', 'GVMRuntime_Samples', 'GVMRHI_Samples', 'GVMRuntime_ThreeSamples'];
const sourceExtensions = new Set(['.cpp', '.c', '.h', '.hpp', '.m', '.mm', '.cmake', '.mjs', '.json', '.yml']);

/** Computes a streaming SHA-256 digest without loading compiler binaries into memory. */
export async function hashFile(filePath) {
  const hash = createHash('sha256');
  for await (const chunk of createReadStream(filePath)) hash.update(chunk);
  return hash.digest('hex');
}

/** Verifies that a single-compute-fixture executable embeds exactly the freshly compiled MSL and SPIR-V payloads. */
export function verifyEmbeddedComputePayloads(header, executable) {
  const wordsMatch = /computeShaderArtifact_SpirvWords\[\]\s*=\s*\{([\s\S]*?)\};/u.exec(header);
  const sourceMatch = /computeShaderArtifact\s*=\s*UGLC::Generated::MakeShaderArtifact\(\s*__UGL__Global__MSLHeader\s*\+\s*R"\(([\s\S]*?)\)"/u.exec(header);
  const preludeMatch = /__UGL__Global__MSLHeader\s*=\s*R"\(([\s\S]*?)\)"/u.exec(header);
  if (!wordsMatch || !sourceMatch?.[1] || !preludeMatch?.[1]) throw new Error('The single-compute fixture has missing runtime payloads.');
  const words = [...wordsMatch[1].matchAll(/0x([a-fA-F0-9]+)/gu)].map(match => Number.parseInt(match[1], 16));
  if (words.length < 5 || words[0] !== 0x07230203) throw new Error('The freshly compiled SPIR-V is empty or malformed.');
  const spirv = Buffer.alloc(words.length * 4);
  words.forEach((word, index) => spirv.writeUInt32LE(word, index * 4));
  const body = Buffer.from(sourceMatch[1]);
  const prelude = Buffer.from(preludeMatch[1]);
  if (!executable.includes(spirv) || !executable.includes(body) || !executable.includes(prelude)) {
    throw new Error('The GPU executable does not embed the freshly compiled shader payloads; rebuild before measuring.');
  }
  return { embeddedPayloadsVerified: true, spirvBytes: spirv.length, mslBytes: body.length + prelude.length,
    spirvSha256: createHash('sha256').update(spirv).digest('hex'), mslSha256: createHash('sha256').update(prelude).update(body).digest('hex') };
}

/** Collects compiler, runtime and test inputs in deterministic order, excluding generated directories. */
async function collectSourceFiles(root, relative, files) {
  const absolute = path.join(root, relative);
  const stat = await fs.lstat(absolute);
  if (stat.isDirectory()) {
    const entries = (await fs.readdir(absolute)).sort();
    for (const entry of entries) {
      if (['node_modules', 'UGLBin', 'build', '.git', '.cache'].includes(entry)) continue;
      await collectSourceFiles(root, path.join(relative, entry), files);
    }
  } else if (stat.isFile() && (sourceExtensions.has(path.extname(relative)) || path.basename(relative) === 'CMakeLists.txt')) {
    files.push({ path: relative.split(path.sep).join('/'), sha256: await hashFile(absolute) });
  }
}

/** Records a version query without turning an unavailable optional inventory tool into a false success. */
async function queryVersion(command, args) {
  try {
    const result = await executeFile(command, args, { timeout: 15000, maxBuffer: 1024 * 1024 });
    return { command: [command, ...args], output: (result.stdout + result.stderr).trim(), available: true };
  } catch (error) {
    return { command: [command, ...args], available: false, error: error.message };
  }
}

/** Records GPU and driver inventory without collecting display serial numbers or unrelated system information. */
async function queryGpuInventory() {
  if (process.platform === 'darwin') {
    try {
      const { stdout } = await executeFile('system_profiler', ['SPDisplaysDataType', '-json'], { timeout: 15000 });
      const data = JSON.parse(stdout);
      return { available: true, vulkanEnvironment: 'macos-portability', devices: (data.SPDisplaysDataType ?? []).map(device => ({
        name: device.sppci_model ?? device._name ?? '', vendor: device.spdisplays_vendor ?? '', metal: device.spdisplays_metal ?? ''
      })) };
    } catch (error) { return { available: false, error: error.message }; }
  }
  return queryVersion('vulkaninfo', ['--summary']);
}

/** Captures immutable source, compiler and toolchain evidence before executing a test selection. */
export async function captureCompilerEvidence(sourceDir, buildDir) {
  const files = [];
  for (const relative of sourceRoots) await collectSourceFiles(sourceDir, relative, files);
  files.sort((left, right) => left.path.localeCompare(right.path));
  const sourceSha256 = createHash('sha256').update(JSON.stringify(files)).digest('hex');
  const cache = await fs.readFile(path.join(buildDir, 'CMakeCache.txt'), 'utf8');
  const configuration = {};
  for (const line of cache.split(/\r?\n/u)) {
    const match = /^([^:#/][^:]*):[^=]+=(.*)$/u.exec(line);
    if (match && /^(UGLC_.*|GVM_BUILD_.*|GVM_UGLC_.*|CMAKE_(BUILD_TYPE|CXX_COMPILER|CXX_FLAGS.*|OSX_.*|SYSTEM_.*)|CPM_PACKAGE_.*_VERSION)$/u.test(match[1])) {
      configuration[match[1]] = match[2];
    }
  }
  const compilerPath = path.join(buildDir, 'UGLC');
  return {
    schemaVersion: 1,
    sourceSha256,
    sourceFiles: files,
    compiler: { path: compilerPath, sha256: await hashFile(compilerPath) },
    configuration,
    toolchains: await Promise.all([
      queryVersion(configuration.CMAKE_CXX_COMPILER || 'c++', ['--version']),
      queryVersion('cmake', ['--version']),
      ...(process.platform === 'darwin' ? [queryVersion('xcrun', ['--sdk', 'macosx', 'metal', '--version'])] : [])
    ]),
    gpuInventory: await queryGpuInventory(),
    host: { platform: process.platform, arch: process.arch, release: os.release(), cpu: os.cpus()[0]?.model ?? '', node: process.version }
  };
}

/** Rejects source or compiler changes during a run so mixed candidate results cannot satisfy acceptance. */
export function verifyUnchangedCandidate(before, after) {
  if (before.sourceSha256 !== after.sourceSha256 || before.compiler.sha256 !== after.compiler.sha256 ||
      JSON.stringify(before.configuration) !== JSON.stringify(after.configuration)) {
    throw new Error('Source, compiler or configuration changed during validation; this run is not evidence for a single candidate.');
  }
}
