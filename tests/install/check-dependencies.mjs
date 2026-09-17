import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { parseArgs } from 'node:util';

/** Reads the explicit settings of an already built installation consumer. */
function readCMakeCache(buildDirectory) {
  const cache = new Map();
  for (const line of fs.readFileSync(path.join(buildDirectory, 'CMakeCache.txt'), 'utf8').split('\n')) {
    const match = /^([^/#][^:]*):[^=]+=(.*)$/u.exec(line);
    if (match) cache.set(match[1], match[2]);
  }
  return cache;
}

/** Executes one consumer check, keeping its full output in the isolated build tree. */
function checkCommand(command, args, logFile, expectedDiagnostic = '') {
  const result = spawnSync(command, args, { encoding: 'utf8', timeout: 180_000, maxBuffer: 16 * 1024 * 1024 });
  const output = `${result.stdout ?? ''}${result.stderr ?? ''}`;
  fs.writeFileSync(logFile, output);
  assert.ifError(result.error);
  assert.equal(result.signal, null, `Command terminated by a signal; see ${logFile}`);
  if (expectedDiagnostic) {
    assert.notEqual(result.status, 0, `Expected a configuration rejection; see ${logFile}`);
    assert.ok(output.includes(expectedDiagnostic), `Missing expected diagnostic; see ${logFile}`);
  } else {
    assert.equal(result.status, 0, `Command failed; see ${logFile}\n${output.slice(-4000)}`);
  }
}

const { values } = parseArgs({ options: { 'consumer-build': { type: 'string' } } });
assert.ok(values['consumer-build'], 'Specify --consumer-build with a successfully built tests/install project.');
const consumerBuild = path.resolve(values['consumer-build']);
const cache = readCMakeCache(consumerBuild);
const checksDirectory = path.join(consumerBuild, 'dependency-checks');
fs.mkdirSync(checksDirectory, { recursive: true });
const consumerSource = cache.get('CMAKE_HOME_DIRECTORY');
const dependencySource = path.resolve(consumerSource, '../dependencies');
const repositorySource = path.resolve(consumerSource, '../..');
const commonArgs = [];
for (const name of ['CMAKE_PREFIX_PATH', 'CPM_SOURCE_CACHE', 'CMAKE_OSX_SYSROOT']) {
  if (cache.get(name)) commonArgs.push(`-D${name}=${cache.get(name)}`);
}

// A Debug application must safely consume the already built Release SDK.
const debugBuild = path.join(checksDirectory, 'debug-consumer');
const debugArgs = ['-S', consumerSource, '-B', debugBuild, ...commonArgs, '-DCMAKE_BUILD_TYPE=Debug'];
for (const name of ['GVM_UGLC_EXECUTABLE', 'GVM_UGLC_RESOURCE_DIR']) {
  if (cache.get(name)) debugArgs.push(`-D${name}=${cache.get(name)}`);
}
checkCommand('cmake', debugArgs, path.join(checksDirectory, 'debug-configure.log'));
checkCommand('cmake', ['--build', debugBuild, '--parallel', '3'], path.join(checksDirectory, 'debug-build.log'));
checkCommand('ctest', ['--test-dir', debugBuild, '--output-on-failure'], path.join(checksDirectory, 'debug-execution.log'));
console.log('Debug consumer: generated compute, shared layouts, include order and allocation passed.');

for (const mode of ['installed', 'source']) {
  const sourceArgs = mode === 'source' ? [`-DGVM_TEST_SOURCE_DIR=${repositorySource}`,
    '-DGVM_BUILD_UGLC=OFF', '-DGVM_RHI_ENABLE_VULKAN=OFF', '-DGVM_BUILD_TESTS=OFF', '-DGVM_BUILD_SAMPLES=OFF'] : [];
  if (mode === 'source') {
    const buildDirectory = path.join(checksDirectory, 'source-consumer');
    checkCommand('cmake', ['-S', dependencySource, '-B', buildDirectory, ...commonArgs, ...sourceArgs,
      '-DCMAKE_BUILD_TYPE=Debug'], path.join(checksDirectory, 'source-configure.log'));
    checkCommand('cmake', ['--build', buildDirectory, '--parallel', '3'], path.join(checksDirectory, 'source-build.log'));
    checkCommand('ctest', ['--test-dir', buildDirectory, '--output-on-failure'], path.join(checksDirectory, 'source-execution.log'));
    console.log('Source consumer: public dependency targets, layouts, include order and allocation passed.');
  }
  for (const target of ['EABase', 'EASTL', 'glm::glm']) {
    const label = `${mode}-${target.replaceAll(':', '-')}`;
    checkCommand('cmake', ['-S', dependencySource, '-B', path.join(checksDirectory, label),
      ...commonArgs, ...sourceArgs, `-DGVM_TEST_PREEXISTING_TARGET=${target}`],
    path.join(checksDirectory, `${label}.log`), 'GVM requires its own fixed GLM/EASTL dependencies');
    console.log(`${label}: rejected with a dependency ownership diagnostic.`);
  }
}

for (const [label, target, header, definition, diagnostic] of [
  ['glm-alignment', 'GVM::GLM', 'glm/glm.hpp', 'GLM_FORCE_DEFAULT_ALIGNED_GENTYPES=1', 'GVM GLM configuration conflict'],
  ['eastl-layout', 'GVM::EASTL', 'EASTL/vector.h', 'EASTL_NAME_ENABLED=1', 'GVM EASTL configuration conflict'],
]) {
  const sourceDirectory = path.join(checksDirectory, `${label}-source`);
  const buildDirectory = path.join(checksDirectory, label);
  fs.mkdirSync(sourceDirectory, { recursive: true });
  fs.writeFileSync(path.join(sourceDirectory, 'CMakeLists.txt'),
    `cmake_minimum_required(VERSION 3.22)
project(DependencyConflict LANGUAGES CXX)
` +
    `find_package(GVM CONFIG REQUIRED)
add_executable(conflict main.cpp)
` +
    `target_link_libraries(conflict PRIVATE ${target})
` +
    `target_compile_definitions(conflict PRIVATE ${definition})
`);
  fs.writeFileSync(path.join(sourceDirectory, 'main.cpp'), `#include <${header}>
int main() { return 0; }
`);
  checkCommand('cmake', ['-S', sourceDirectory, '-B', buildDirectory, ...commonArgs],
    path.join(checksDirectory, `${label}-configure.log`));
  checkCommand('cmake', ['--build', buildDirectory, '--parallel', '3'],
    path.join(checksDirectory, `${label}-build.log`), diagnostic);
  console.log(`${label}: rejected with a configuration diagnostic.`);
}
