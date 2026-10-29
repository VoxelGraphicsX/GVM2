import { promises as fs } from 'node:fs';
import path from 'node:path';

/** Reads the existing build configuration without hashing inputs or querying compiler and GPU processes. */
export async function captureCompilerEvidence(sourceDir, buildDir) {
  const cache = await fs.readFile(path.join(buildDir, 'CMakeCache.txt'), 'utf8');
  const configuration = {};
  for (const line of cache.split(/\r?\n/u)) {
    const match = /^([^:#/][^:]*):[^=]+=(.*)$/u.exec(line);
    if (match && /^(UGLC_.*|GVM_BUILD_.*|GVM_UGLC_.*|CMAKE_(BUILD_TYPE|CXX_COMPILER|CXX_FLAGS.*|OSX_.*|SYSTEM_.*)|CPM_PACKAGE_.*_VERSION)$/u.test(match[1])) {
      configuration[match[1]] = match[2];
    }
  }
  return { schemaVersion: 1, sourceDir, compiler: { path: configuration.GVM_UGLC_EXECUTABLE || path.join(buildDir, 'UGLC') }, configuration,
    host: { platform: process.platform, arch: process.arch, node: process.version } };
}
