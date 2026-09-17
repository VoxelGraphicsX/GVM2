import path from 'node:path';

export function getDefaultProfileName() {
  return 'macos-debug';
}

function resolvePath(sourceDir, configuredPath, fallbackPath) {
  if (!configuredPath) {
    return fallbackPath;
  }
  return path.isAbsolute(configuredPath)
    ? configuredPath
    : path.join(sourceDir, configuredPath);
}

export function getProfile(sourceDir, profileName = getDefaultProfileName()) {
  const profiles = {
    'macos-debug': {
      name: 'macos-debug',
      generator: 'Unix Makefiles',
      binaryDir: resolvePath(
        sourceDir,
        process.env.GVM_TEST_BUILD_DIR,
        path.join(sourceDir, 'build', 'macos-tests-debug-make')
      ),
      buildType: 'Debug',
      buildJobs: 4,
      cmakeCache: {
        CPM_USE_LOCAL_PACKAGES: 'ON',
        GVM_BUILD_SAMPLES: 'OFF',
        GVM_BUILD_TESTS: 'ON',
        GVM_BUILD_TESTS_UNIT: 'ON',
        GVM_BUILD_TESTS_RHI: 'ON',
        GVM_BUILD_TESTS_DSL: 'ON',
        GVM_BUILD_TESTS_RENDER: 'ON',
        GVM_BUILD_UGLC: 'ON'
      }
    }
  };

  if (profileName === 'vulkan-debug') {
    return { ...profiles['macos-debug'], name: profileName,
      binaryDir: path.join(sourceDir, 'build', 'vulkan-tests-debug-make') };
  }
  const profile = profiles[profileName];
  if (!profile) {
    throw new Error(`Unknown test profile: ${profileName}`);
  }
  return profile;
}
