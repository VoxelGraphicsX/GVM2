import assert from 'node:assert/strict';
import { promises as fs } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import {
  lintSampleGpuBoundary,
  parseSampleGpuBoundaryArguments,
  scanSampleCppSource,
  tokenizeCppSource
} from './lint_sample_gpu_boundary.mjs';

const toolsDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(toolsDirectory, '..', '..');

/** Creates the fixed directory layout required by the standalone boundary scanner. */
async function createFixtureRoot() {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'gvm-three-gpu-boundary-'));
  for (const relativeDirectory of [
    'GVMRuntime_ThreeSamples/Host',
    'GVMRuntime_ThreeSamples/Fixtures',
    'GVMRuntime_ThreeSamples/ThreeCompat'
  ]) {
    await fs.mkdir(path.join(root, ...relativeDirectory.split('/')), { recursive: true });
  }
  return root;
}

/** Writes one UTF-8 fixture source below a temporary repository root. */
async function writeFixtureSource(root, relativePath, source) {
  const absolutePath = path.join(root, ...relativePath.split('/'));
  await fs.mkdir(path.dirname(absolutePath), { recursive: true });
  await fs.writeFile(absolutePath, source, 'utf8');
}

test('tokenizer masks comments and literals while preserving line offsets', () => {
  const source = `// encoder->draw();
const char *message = "device->dispatch()";
const char *shader = R"dsl(@vertex fn main() {})dsl";
renderer.update();
`;
  const tokenized = tokenizeCppSource(source);
  assert.equal(tokenized.code.split('\n').length, source.split('\n').length);
  assert.doesNotMatch(tokenized.code, /draw|dispatch|@vertex/u);
  assert.match(tokenized.code, /renderer\.update/u);
  assert.deepEqual(tokenized.strings.map((entry) => entry.value), [
    'device->dispatch()',
    '@vertex fn main() {}'
  ]);
});

test('allows generated RenderSet updates, texture uploads, image decode, and readback', () => {
  const source = `
void updateGeneratedScene(AbstractRendererImpl &renderer, Device &device) {
  const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
  encoder->allocEntity(allocation);
  encoder->removeEntity(entity);
  renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
  renderer.updateTexture(TextureHandle, pixels.data(), pixels.size());
  device.getCommandQueue()->writeBuffer(buffer, bytes.data(), bytes.size());
  device.getCommandQueue()->writeTexture(texture, pixels.data(), pixels.size());
  device.getCommandQueue()->readTexture(readbackTexture, rgba.data(), rgba.size()).wait();
  CGContextDrawImage(context, bounds, decodedImage);
}
`;
  assert.deepEqual(scanSampleCppSource(source, 'Fixtures/Allowed.cpp'), []);
});

test('reports direct GPU work, handwritten shaders, software rasterization, and reference bypasses', () => {
  const source = `
#include "vulkan/vulkan.h"
void bypass(Device *device, Encoder *encoder) {
  RenderPipelineDescriptor descriptor;
  RenderPassTaskDescriptor directTask;
  const auto pipeline = device->createRenderPipeline(descriptor);
  const auto pass = encoder->beginRenderPass(renderTarget);
  pass->drawIndexedIndirect(indirectBuffer);
  pass->dispatchWorkgroups(8, 8, 1);
  pass->presentDrawable(drawable);
  rasterizeTriangle(vertices);
  rasterizeRetainedPathStroke(canvas, segments, 4);
  const auto referencePixels = loadReferenceImage("oracle/reference-screenshot.png");
}
const char *shaderPath = "shaders/manual.spv";
const char *shaderSource = R"wgsl(@vertex fn vertexMain() -> @builtin(position) vec4f { return vec4f(); })wgsl";
static const unsigned int embeddedShader[] = { 0x07230203 };
`;
  const violations = scanSampleCppSource(source, 'Fixtures/Forbidden.cpp');
  const ruleIds = new Set(violations.map((violation) => violation.ruleId));
  assert.equal(
    violations.filter((violation) => violation.ruleId === 'software-rasterizer.call').length,
    2
  );
  for (const expectedRule of [
    'gpu.native-header',
    'gpu.pipeline.direct-descriptor',
    'gpu.pipeline.direct-create',
    'gpu.pass.direct-encode',
    'gpu.draw.direct-call',
    'gpu.dispatch.direct-call',
    'gpu.blit-present.direct-call',
    'software-rasterizer.call',
    'reference-image.bypass-call',
    'reference-image.bypass-identifier',
    'reference-image.bypass-path',
    'shader.handwritten-path',
    'shader.handwritten-source',
    'shader.embedded-spirv-magic'
  ]) {
    assert.ok(ruleIds.has(expectedRule), `missing ${expectedRule}`);
  }
  assert.ok(violations.every((violation) => (
    violation.file === 'Fixtures/Forbidden.cpp'
      && violation.line > 0
      && violation.column > 0
      && violation.snippet.length > 0
  )));
});

test('does not treat forbidden vocabulary in comments or diagnostics as API calls', () => {
  const source = `
// device->createRenderPipeline(descriptor);
/* pass->drawIndexedIndirect(buffer); */
const char *diagnostic = "beginRenderPass draw dispatch are forbidden here";
void recordSnapshot() {
  output << "drawCommandCount" << 1;
  output << "computePassCount" << 1;
}
`;
  assert.deepEqual(scanSampleCppSource(source, 'Host/Diagnostics.cpp'), []);
});

test('directory scan is deterministic and returns structured category totals', async (t) => {
  const root = await createFixtureRoot();
  t.after(() => fs.rm(root, { recursive: true, force: true }));
  await writeFixtureSource(
    root,
    'GVMRuntime_ThreeSamples/Host/Allowed.cpp',
    'void update(Renderer &renderer) { renderer.updateTexture(handle, bytes, size); }\n'
  );
  await writeFixtureSource(
    root,
    'GVMRuntime_ThreeSamples/Fixtures/Forbidden.hpp',
    'void encode(Pass &pass) { pass.drawIndexed(3); }\n'
  );
  await writeFixtureSource(
    root,
    'GVMRuntime_ThreeSamples/ThreeCompat/Ignored.mjs',
    'encoder.draw();\n'
  );

  const first = await lintSampleGpuBoundary(root);
  const second = await lintSampleGpuBoundary(root);
  assert.deepEqual(first, second);
  assert.equal(first.status, 'fail');
  assert.equal(first.filesScanned, 2);
  assert.equal(first.violationCount, 1);
  assert.deepEqual(first.summary.categories, { 'direct-draw': 1 });
  assert.equal(first.violations[0].ruleId, 'gpu.draw.direct-call');
});

test('CLI parser requires an explicit source root and rejects unknown configuration', () => {
  assert.deepEqual(parseSampleGpuBoundaryArguments([
    '--source-root', '/workspace/GVM', '--format', 'json'
  ]), {
    sourceRoot: '/workspace/GVM',
    format: 'json',
    help: false
  });
  assert.throws(() => parseSampleGpuBoundaryArguments([]), /--source-root is required/u);
  assert.throws(() => parseSampleGpuBoundaryArguments([
    '--source-root', '/workspace/GVM', '--format', 'xml'
  ]), /text or json/u);
  assert.throws(() => parseSampleGpuBoundaryArguments([
    '--source-root', '/workspace/GVM', '--environment', 'unsafe'
  ]), /Unknown argument/u);
});

test('current Three Sample C++ boundary contains no direct GPU bypass', async () => {
  const result = await lintSampleGpuBoundary(repositoryRoot);
  assert.equal(result.status, 'pass', result.violations.map((violation) => (
    `${violation.file}:${violation.line} [${violation.ruleId}]`
  )).join('\n'));
  assert.ok(result.filesScanned > 0);
});
