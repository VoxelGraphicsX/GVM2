/** Lists mandatory experimental executables independently of the registry to detect lost registrations. */
export const requiredUglirTargets = Object.freeze([
  'gvm_unit_uglc_spirv_validation',
  'gvm_rhi_uglir_evaluation_semantics_tests',
  'gvm_rhi_uglir_unoptimized_evaluation_semantics_tests',
  'gvm_rhi_uglir_buffer_layout_semantics_tests',
  'gvm_rhi_uglir_unoptimized_buffer_layout_semantics_tests',
  'gvm_dsl_uglir_codegen_smoke_tests',
  'gvm_dsl_uglir_storage_codegen_tests',
  'gvm_dsl_uglir_texture_codegen_tests',
  'gvm_dsl_uglir_cube_codegen_tests',
  'gvm_dsl_uglir_render_feature_codegen_tests',
  'gvm_dsl_uglir_atomic_codegen_tests',
  'gvm_rhi_uglir_uniform_buffer_tests',
  'gvm_rhi_uglir_uniform_render_tests',
  'gvm_rhi_uglir_wvm_regression_tests',
  'gvm_rhi_uglir_headless_readback_tests',
  'gvm_rhi_uglir_native_half_tests',
  'gvm_rhi_uglir_spd_hiz_readback_tests',
  'gvm_rhi_uglir_texture3d_readwrite_tests',
  'gvm_rhi_uglir_texture2darray_readwrite_tests',
  'gvm_rhi_uglir_vulkan_texture_role_transition_tests',
  'gvm_rhi_uglir_spark_lod_traversal_tests',
  'gvm_rhi_uglir_cluster16_traversal_tests',
  'gvm_rhi_uglir_gpu_radix_sort_tests',
  'gvm_dsl_uglir_render_feature_tests'
]);

/** Rejects duplicate targets and missing UGLIR or enabled Legacy registrations. */
export function validateUglirSelection(legacyEnabled, registry, selection, requireFullSuite = false) {
  const names = registry.map(entry => entry.target);
  if (names.some(name => typeof name !== 'string' || !name) || new Set(names).size !== names.length)
    throw new Error('The GVM registry contains missing or duplicate target IDs.');
  const required = [...requiredUglirTargets, ...(legacyEnabled ? [
    'gvm_rhi_legacy_evaluation_semantics_tests', 'gvm_rhi_legacy_buffer_layout_semantics_tests'] : [])];
  const missing = required.filter(name => !names.includes(name));
  if (missing.length) throw new Error('Required UGLIR targets are not registered: ' + missing.join(', '));
  if (!selection.length) throw new Error('The selection contains no executable tests.');
  if (requireFullSuite) {
    const selected = new Set(selection.map(entry => entry.target));
    const omitted = required.filter(name => !selected.has(name));
    if (omitted.length) throw new Error('Full UGLIR selection omits required targets: ' + omitted.join(', '));
  }
}

/** Validates an actual GoogleTest report and counts executed cases; process success alone is insufficient. */
export function inspectGoogleTestReport(report) {
  if (!report || !Number.isInteger(report.tests) || report.tests <= 0 || !Array.isArray(report.testsuites)) {
    throw new Error('The test executable did not report any registered GoogleTest cases.');
  }
  const cases = report.testsuites.flatMap(suite => Array.isArray(suite.testsuite) ? suite.testsuite : []);
  if (cases.length !== report.tests) {
    throw new Error(`GoogleTest report count mismatch: declared ${report.tests}, recorded ${cases.length}.`);
  }
  const executed = cases.filter(entry => entry.status === 'RUN' && entry.result !== 'SKIPPED' && entry.result !== 'SUPPRESSED');
  const failures = executed.filter(entry => Array.isArray(entry.failures) && entry.failures.length > 0);
  return { registered: cases.length, executed: executed.length, failures: failures.length, skipped: cases.length - executed.length };
}

/** Requires successful GPU readback on the requested backend and, for sustained tests, measured execution duration and iterations. */
export function inspectReadbackReport(report, backend, minimumDurationMs = 0) {
  const counts = inspectGoogleTestReport(report);
  if (counts.executed !== counts.registered || counts.failures > 0) throw new Error('Readback evidence contains failures or skipped cases.');
  const cases = report.testsuites.flatMap(suite => suite.testsuite);
  for (const entry of cases) {
    if (entry.backend !== backend) throw new Error('The readback executable did not verify the requested backend.');
    if (minimumDurationMs > 0) {
      const duration = Number(entry.readback_duration_ms);
      const iterations = Number(entry.readback_iterations);
      if (!Number.isFinite(duration) || duration < minimumDurationMs || !Number.isInteger(iterations) || iterations < 2) {
        throw new Error('Readback did not execute for the required duration and iterations.');
      }
    }
  }
  return cases;
}
