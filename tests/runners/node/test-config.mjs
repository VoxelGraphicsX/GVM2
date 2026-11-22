import path from 'node:path';

export function getDefaultProfileName() {
  if (process.platform === 'win32') {
    return 'windows-debug';
  }
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
        GVM_BUILD_UGLC: 'ON',
        GVM_ENABLE_UPSTREAM_MARL: 'ON'
      }
    },
    'macos-asan': {
      name: 'macos-asan',
      generator: 'Unix Makefiles',
      binaryDir: resolvePath(
        sourceDir,
        process.env.GVM_TEST_BUILD_DIR_ASAN,
        path.join(sourceDir, 'build', 'macos-tests-asan-make')
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
        GVM_BUILD_UGLC: 'ON',
        GVM_ENABLE_UPSTREAM_MARL: 'ON',
        GVM_SANITIZERS: 'address,undefined'
      },
      environment: {
        ASAN_OPTIONS: 'halt_on_error=0:print_stacktrace=1',
        UBSAN_OPTIONS: `halt_on_error=0:print_stacktrace=1:suppressions=${path.join(sourceDir, 'tests', 'sanitizers', 'ubsan.supp')}`
      }
    },
    'macos-overlay-off': {
      name: 'macos-overlay-off',
      generator: 'Unix Makefiles',
      binaryDir: path.join(sourceDir, 'build', 'macos-tests-overlay-off-make'),
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
        GVM_BUILD_UGLC: 'ON',
        GVM_ENABLE_UPSTREAM_MARL: 'ON',
        GVM_RHI_ENABLE_DIAGNOSTICS_OVERLAY: 'OFF'
      }
    },
    'macos-cpu-probes': {
      name: 'macos-cpu-probes',
      generator: 'Unix Makefiles',
      binaryDir: path.join(sourceDir, 'build', 'macos-tests-cpu-probes-make'),
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
        GVM_BUILD_UGLC: 'ON',
        GVM_ENABLE_UPSTREAM_MARL: 'ON',
        GVM_RHI_ENABLE_LOGGING: 'ON',
        GVM_RHI_ENABLE_CPU_PROBES: 'ON'
      }
    },
    'macos-tsan': {
      name: 'macos-tsan',
      generator: 'Unix Makefiles',
      binaryDir: resolvePath(
        sourceDir,
        process.env.GVM_TEST_BUILD_DIR_TSAN,
        path.join(sourceDir, 'build', 'macos-tests-tsan-make')
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
        GVM_BUILD_UGLC: 'ON',
        GVM_ENABLE_UPSTREAM_MARL: 'ON',
        GVM_SANITIZERS: 'thread'
      },
      environment: {
        TSAN_OPTIONS: `halt_on_error=0:print_stacktrace=1:suppressions=${path.join(sourceDir, 'tests', 'sanitizers', 'tsan.supp')}`
      }
    },
    'windows-debug': {
      name: 'windows-debug',
      generator: 'Ninja',
      binaryDir: resolvePath(
        sourceDir,
        process.env.GVM_TEST_BUILD_DIR,
        path.join(sourceDir, 'build', 'windows-tests-debug-ninja')
      ),
      buildType: 'Debug',
      buildJobs: 8,
      cmakeCache: {
        CPM_USE_LOCAL_PACKAGES: 'ON',
        GVM_BUILD_SAMPLES: 'OFF',
        GVM_BUILD_TESTS: 'ON',
        GVM_BUILD_TESTS_UNIT: 'ON',
        GVM_BUILD_TESTS_RHI: 'ON',
        GVM_BUILD_TESTS_DSL: 'ON',
        GVM_BUILD_TESTS_RENDER: 'ON',
        GVM_BUILD_UGLC: 'ON',
        GVM_ENABLE_UPSTREAM_MARL: 'ON'
      }
    },
    'windows-overlay-off': {
      name: 'windows-overlay-off',
      generator: 'Ninja',
      binaryDir: path.join(sourceDir, 'build', 'windows-tests-overlay-off-ninja'),
      buildType: 'Debug',
      buildJobs: 8,
      cmakeCache: {
        CPM_USE_LOCAL_PACKAGES: 'ON',
        GVM_BUILD_SAMPLES: 'OFF',
        GVM_BUILD_TESTS: 'ON',
        GVM_BUILD_TESTS_UNIT: 'ON',
        GVM_BUILD_TESTS_RHI: 'ON',
        GVM_BUILD_TESTS_DSL: 'ON',
        GVM_BUILD_TESTS_RENDER: 'ON',
        GVM_BUILD_UGLC: 'ON',
        GVM_ENABLE_UPSTREAM_MARL: 'ON',
        GVM_RHI_ENABLE_DIAGNOSTICS_OVERLAY: 'OFF'
      }
    },
    'windows-cpu-probes': {
      name: 'windows-cpu-probes',
      generator: 'Ninja',
      binaryDir: path.join(sourceDir, 'build', 'windows-tests-cpu-probes-ninja'),
      buildType: 'Debug',
      buildJobs: 8,
      cmakeCache: {
        CPM_USE_LOCAL_PACKAGES: 'ON',
        GVM_BUILD_SAMPLES: 'OFF',
        GVM_BUILD_TESTS: 'ON',
        GVM_BUILD_TESTS_UNIT: 'ON',
        GVM_BUILD_TESTS_RHI: 'ON',
        GVM_BUILD_TESTS_DSL: 'ON',
        GVM_BUILD_TESTS_RENDER: 'ON',
        GVM_BUILD_UGLC: 'ON',
        GVM_ENABLE_UPSTREAM_MARL: 'ON',
        GVM_RHI_ENABLE_LOGGING: 'ON',
        GVM_RHI_ENABLE_CPU_PROBES: 'ON'
      }
    }
  };

  if (profileName === 'vulkan-debug') {
    const baseProfile = process.platform === 'win32'
      ? profiles['windows-debug']
      : profiles['macos-debug'];
    const vulkanBuildLeaf = process.platform === 'win32'
      ? 'vulkan-tests-debug-ninja'
      : 'vulkan-tests-debug-make';
    return {
      ...baseProfile,
      name: profileName,
      binaryDir: path.join(sourceDir, 'build', vulkanBuildLeaf)
    };
  }
  const profile = profiles[profileName];
  if (!profile) {
    throw new Error(`Unknown test profile: ${profileName}`);
  }
  return profile;
}
