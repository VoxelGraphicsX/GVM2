#!/usr/bin/env node

import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { pathToFileURL } from 'node:url';

const artifactCases = Object.freeze([
  {
    name: 'WebglShader',
    passName: 'WebglShaderMonjoriPass',
    expectsStandaloneGeometry: false,
    expectedDrawCount: 1,
    drawPattern: /->run\(3u, 1u, 0u, 0u\)/gu,
    experimentalStageChecks: {
      fragment: ['intrinsic math_sin', 'intrinsic math_cos', 'intrinsic math_tan', 'intrinsic math_fmod']
    }
  },
  {
    name: 'WebglBuffergeometryAttributesNone',
    passName: 'WebglBuffergeometryAttributesNoneScenePass',
    expectsStandaloneGeometry: false,
    expectedDrawCount: 4,
    drawPattern: /->run\(WebglBuffergeometryAttributesNoneVertexCount, 1u, 0u, 0u\)/gu,
    experimentalStageChecks: {
      vertex: ['bitcast_asuint', 'bitcast_asfloat']
    }
  },
  {
    name: 'WebglBuffergeometryRawshader',
    passName: 'WebglBuffergeometryRawshaderMainPass',
    expectsStandaloneGeometry: true,
    expectedDrawCount: 1,
    drawPattern: /->run\(WebglBuffergeometryRawshaderVertexCount, 1u, 0u, 0u\)/gu,
    experimentalStageChecks: {
      fragment: ['intrinsic math_sin']
    }
  }
]);

/** Parses strict value-bearing long options accepted by the standalone lint command. */
function parseArguments(argv) {
  const options = {};
  for (let index = 2; index < argv.length; index += 2) {
    const option = argv[index];
    const value = argv[index + 1];
    if (!option?.startsWith('--') || value == null || value.startsWith('--')) {
      throw new Error(`Expected --option value pair near '${option ?? '<end>'}'.`);
    }
    options[option.slice(2)] = value;
  }
  return options;
}

/** Returns whether one generated artifact path exists. */
async function pathExists(targetPath) {
  try {
    await fs.access(targetPath);
    return true;
  } catch {
    return false;
  }
}

/** Reads one required generated text artifact or records a deterministic failure. */
async function readRequiredText(targetPath, failures, label) {
  if (!await pathExists(targetPath)) {
    failures.push(`${label}: missing ${targetPath}.`);
    return '';
  }
  return fs.readFile(targetPath, 'utf8');
}

/** Validates one case/pipeline product set without depending on manifest state. */
async function lintCasePipeline(generatedRoot, definition, pipeline) {
  const generatedDirectory = path.join(generatedRoot, pipeline, definition.name, 'UGLBin');
  const label = `${definition.name}/${pipeline}`;
  const failures = [];
  const exportsSource = await readRequiredText(
    path.join(generatedDirectory, 'exports.hpp'), failures, label);
  const generatedSource = await readRequiredText(
    path.join(generatedDirectory, 'generate_result.hpp'), failures, label);

  if (exportsSource && !/namespace\s+ExportedRenderSet\s*\{\s*\};/u.test(exportsSource)) {
    failures.push(`${label}: RenderSet export namespace must remain empty.`);
  }
  if (generatedSource.includes('RenderSet<')) {
    failures.push(`${label}: simple r185 shader case unexpectedly generated a RenderSet.`);
  }
  if (!definition.expectsStandaloneGeometry &&
      (generatedSource.includes('setVertexBuffer') || generatedSource.includes('setIndexBuffer'))) {
    failures.push(`${label}: procedural shader case unexpectedly binds geometry buffers.`);
  }
  if (definition.expectsStandaloneGeometry) {
    const vertexBufferBindings = generatedSource.match(/mainPass->setVertexBuffer\(vertexBuffer\)/gu) ?? [];
    if (vertexBufferBindings.length !== 1 || generatedSource.includes('setIndexBuffer')) {
      failures.push(`${label}: raw-shader geometry must bind one standalone vertex buffer and no index buffer.`);
    }
  }
  if (!generatedSource.includes(`class ${definition.passName}`)) {
    failures.push(`${label}: generated host is missing ${definition.passName}.`);
  }
  const drawCount = [...generatedSource.matchAll(definition.drawPattern)].length;
  if (drawCount !== definition.expectedDrawCount) {
    failures.push(`${label}: expected ${definition.expectedDrawCount} locked explicit draw(s), observed ${drawCount}.`);
  }
  if (!generatedSource.includes('BufferUsage::Uniform') ||
      !generatedSource.includes('BufferUsage::CopyDst')) {
    failures.push(`${label}: generated host is missing the existing uniform/CopyDst buffer contract.`);
  }
  if (definition.name === 'WebglBuffergeometryAttributesNone' &&
      !generatedSource.includes('WebglBuffergeometryAttributesNoneVertexCount = 30000u')) {
    failures.push(`${label}: generated host does not preserve the locked 30,000-vertex draw count.`);
  }
  if (definition.name === 'WebglBuffergeometryRawshader') {
    for (const token of [
      'WebglBuffergeometryRawshaderVertexCount = 600u',
      'GVM::RHI::BufferUsage::Vertex',
      'GVM::RHI::BufferUsage::CopyDst',
      'GVM::RHI::VertexFormat::Float32x3',
      'GVM::RHI::VertexFormat::Uint32',
      'GVM::RHI::BlendFactor::SrcAlpha',
      'GVM::RHI::BlendFactor::OneMinusSrcAlpha',
      'renderPass("main-private-shader"'
    ]) {
      if (!generatedSource.includes(token)) {
        failures.push(`${label}: generated host is missing '${token}'.`);
      }
    }
    if (generatedSource.includes('RenderSet<') || generatedSource.includes('setIndexBuffer')) {
      failures.push(`${label}: single-object raw-shader Scene must remain non-indexed and RenderSet-free.`);
    }
  }

  if (pipeline === 'experimental') {
    for (const stage of ['vertex', 'fragment']) {
      const baseName = `${definition.passName}__${stage}`;
      const products = [
        path.join(generatedDirectory, 'uglir', `${baseName}.uglir.json`),
        path.join(generatedDirectory, 'uglir', `${baseName}.uglir.txt`),
        path.join(generatedDirectory, 'msl', `${baseName}.msl`),
        path.join(generatedDirectory, 'spv', `${baseName}.raw.spv.txt`),
        path.join(generatedDirectory, 'spv', `${baseName}.raw.spvasm`)
      ];
      for (const product of products) {
        if (!await pathExists(product)) {
          failures.push(`${label}: missing ${path.relative(generatedDirectory, product)}.`);
        }
      }
    }

    for (const [stage, requiredTokens] of Object.entries(definition.experimentalStageChecks)) {
      const uglirPath = path.join(
        generatedDirectory, 'uglir', `${definition.passName}__${stage}.uglir.txt`);
      const uglirSource = await readRequiredText(uglirPath, failures, label);
      for (const token of requiredTokens) {
        if (!uglirSource.includes(token)) {
          failures.push(`${label}: ${stage} UGLIR is missing '${token}'.`);
        }
      }
    }

    if (definition.name === 'WebglBuffergeometryAttributesNone') {
      const mslSource = await readRequiredText(
        path.join(generatedDirectory, 'msl', `${definition.passName}__vertex.msl`),
        failures,
        label);
      const spirvSource = await readRequiredText(
        path.join(generatedDirectory, 'spv', `${definition.passName}__vertex.raw.spvasm`),
        failures,
        label);
      if (!mslSource.includes('as_type<uint>') || !mslSource.includes('as_type<float>')) {
        failures.push(`${label}: MSL vertex output must preserve uint/float scalar bitcasts.`);
      }
      if ((spirvSource.match(/OpBitcast/gu) ?? []).length < 3) {
        failures.push(`${label}: direct SPIR-V vertex output must preserve all scalar bitcasts.`);
      }
    }
  }

  return { case: definition.name, pipeline, status: failures.length === 0 ? 'pass' : 'fail', failures };
}

/** Lints all locked cases across isolated Legacy and Experimental generated directories. */
export async function lintPhase1ShaderCaseArtifacts(generatedRoot) {
  const entries = [];
  for (const definition of artifactCases) {
    for (const pipeline of ['legacy', 'experimental']) {
      entries.push(await lintCasePipeline(generatedRoot, definition, pipeline));
    }
  }
  return {
    schemaVersion: 1,
    gate: 'three-r185-phase1-shader-generated-artifacts',
    status: entries.every((entry) => entry.status === 'pass') ? 'pass' : 'fail',
    entries
  };
}

/** Runs the lint as a standalone explicit-path command. */
async function main() {
  const options = parseArguments(process.argv);
  if (!options['generated-root']) {
    throw new Error('Missing required --generated-root path.');
  }
  const result = await lintPhase1ShaderCaseArtifacts(path.resolve(options['generated-root']));
  for (const entry of result.entries) {
    console.log(`[${entry.status.toUpperCase()}] ${entry.case} ${entry.pipeline}`);
    for (const failure of entry.failures) {
      console.log(`  ${failure}`);
    }
  }
  if (options['report-json']) {
    const reportPath = path.resolve(options['report-json']);
    await fs.mkdir(path.dirname(reportPath), { recursive: true });
    await fs.writeFile(reportPath, `${JSON.stringify(result, null, 2)}\n`, 'utf8');
  }
  if (result.status !== 'pass') {
    process.exitCode = 1;
  }
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
