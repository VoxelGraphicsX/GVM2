/** Required experimental cases are frozen independently of registry selection to detect omissions. */
export const requiredUglirCaseIds = Object.freeze([
  "experimental-uglir-unoptimized-evaluation-semantics",
  "experimental-uglir-optimization-option-requires-entry",
  "experimental-uglir-buffer-layout-semantics",
  "experimental-uglir-invalid-layout-float3-storage",
  "experimental-uglir-invalid-layout-struct-tail-storage",
  "experimental-uglir-invalid-layout-scalar-vector-storage",
  "experimental-uglir-invalid-layout-three-component-matrix-storage",
  "experimental-uglir-invalid-layout-narrow-uniform-matrix",
  "experimental-uglir-excluded-hull-domain",
  "experimental-uglir-failure-invalidates-stale-artifacts",
  "experimental-uglir-invariant-validation",
  "experimental-uglir-undefined-function",
  "experimental-uglir-invalid-destructor",
  "experimental-uglir-invalid-integer-width",
  "experimental-uglir-invalid-floating-width",
  "experimental-uglir-compute-basic-msl-direct-spirv",
  "experimental-uglir-compute-basic-shadow",
  "experimental-uglir-compute-control-flow-direct-spirv-artifact",
  "experimental-uglir-concepts-requires-policy",
  "experimental-uglir-evaluation-semantics",
  "experimental-uglir-explicit-precision-construct-info",
  "experimental-uglir-intrinsic-call-kinds-compute",
  "experimental-uglir-intrinsic-call-kinds-fragment",
  "experimental-uglir-invalid-address-of",
  "experimental-uglir-invalid-coroutine",
  "experimental-uglir-invalid-direct-recursion",
  "experimental-uglir-invalid-escaping-lambda",
  "experimental-uglir-invalid-exception",
  "experimental-uglir-invalid-function-pointer",
  "experimental-uglir-invalid-generic-lambda",
  "experimental-uglir-invalid-lambda-host-pointer-capture",
  "experimental-uglir-invalid-lambda-reference-capture",
  "experimental-uglir-invalid-member-pointer",
  "experimental-uglir-invalid-mutual-recursion",
  "experimental-uglir-invalid-new-delete",
  "experimental-uglir-invalid-nullptr",
  "experimental-uglir-invalid-raw-pointer",
  "experimental-uglir-invalid-rtti",
  "experimental-uglir-invalid-spirv-binary-operator",
  "experimental-uglir-invalid-stage-method",
  "experimental-uglir-invalid-stl-container",
  "experimental-uglir-invalid-thread",
  "experimental-uglir-invalid-uniform-scalar",
  "experimental-uglir-invalid-virtual-dispatch",
  "experimental-uglir-lambda-compute",
  "experimental-uglir-matrix-vector-mul-vulkan",
  "experimental-uglir-invalid-non-square-vector-matrix-mul",
  "experimental-uglir-invalid-non-square-matrix-matrix-mul",
  "experimental-uglir-native-half-spirv",
  "experimental-uglir-if-constexpr-std-bool",
  "experimental-uglir-invalid-dsl-reserved-vertex-local",
  "experimental-uglir-invalid-dsl-reserved-vertex-parameter",
  "experimental-uglir-invalid-dsl-reserved-vertex-field",
  "experimental-uglir-invalid-global-mutable-read",
  "experimental-uglir-invalid-static-mutable-local",
  "experimental-uglir-invalid-dynamic-constant-storage",
  "experimental-uglir-invalid-mutable-reference-non-memory-object",
  "experimental-uglir-invalid-value-result-reference-address-space",
  "experimental-uglir-pixel-local-deferred-screen",
  "experimental-uglir-render-basic-msl-direct-spirv",
  "experimental-uglir-render-set-shader-abi",
  "experimental-uglir-rgba32float-framebuffer",
  "experimental-uglir-stage-io-semantics",
  "experimental-uglir-static-variant-compute-template",
  "experimental-uglir-static-variant-render",
  "experimental-uglir-storage-texture-compute",
  "experimental-uglir-symbolic-resources",
  "experimental-uglir-symbolic-value-types",
  "experimental-uglir-template-policy-class-callback",
  "experimental-uglir-texture2darray-gather",
  "experimental-uglir-uniform-buffer-compute",
  "experimental-uglir-wvm-regressions",
  "uglir-core-dump"
]);

/** Rejects missing registrations, duplicate IDs, and incomplete full-suite selections. */
export function validateUglirSelection(profile, registry, selectedCases, requireFullSuite = false) {
  const ids = registry.map(entry => entry.id);
  if (ids.some(id => typeof id !== 'string' || !id) || new Set(ids).size !== ids.length)
    throw new Error('The UGLC registry contains missing or duplicate test IDs.');
  const missing = requiredUglirCaseIds.filter(id => !ids.includes(id));
  if (missing.length) throw new Error('Required UGLIR tests are not registered: ' + missing.join(', '));
  if (!selectedCases.some(entry => !entry.skipReason)) throw new Error('The selection contains no executable tests.');
  if (requireFullSuite) {
    const selected = new Set(selectedCases.map(entry => entry.id));
    const omitted = requiredUglirCaseIds.filter(id => !selected.has(id));
    if (omitted.length) throw new Error('Full UGLIR selection omits required tests: ' + omitted.join(', '));
  }
}
